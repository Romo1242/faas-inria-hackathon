#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include "ipc.h"
#include "storage.h"

#define HTTP_PORT 8080
#define RECV_BUF 8192
#define MAX_BODY 1024*1024 // 1MB max

// API Gateway no longer compiles - it delegates to Server

static int start_http_server(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) die("bind");
    if (listen(fd, 128) < 0) die("listen");
    return fd;
}

static void send_http(int cfd, int status, const char *status_text, const char *body, const char *ctype) {
    char hdr[512];
    int blen = body ? (int)strlen(body) : 0;
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        status, status_text, ctype ? ctype : "text/plain", blen);
    write_all(cfd, hdr, (size_t)n);
    if (blen > 0) write_all(cfd, body, (size_t)blen);
}

static int parse_content_length(const char *hdrs) {
    const char *p = strcasestr(hdrs, "Content-Length:");
    if (!p) return -1;
    p += strlen("Content-Length:");
    while (*p == ' ') p++;
    return atoi(p);
}

static void handle_deploy(int cfd, const char *buf, ssize_t n);
static void handle_invoke(int cfd, const char *buf, ssize_t n);

static void handle_client(int cfd) {
    char buf[RECV_BUF];
    ssize_t n = recv(cfd, buf, sizeof(buf)-1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    // Route requests
    if (strncmp(buf, "POST /deploy", 12) == 0) {
        handle_deploy(cfd, buf, n);
        return;
    } else if (strncmp(buf, "POST /invoke", 12) == 0) {
        handle_invoke(cfd, buf, n);
        return;
    } else if (strncmp(buf, "GET /function/", 14) == 0) {
        // Extract function name from path
        const char *path_start = buf + 14; // Skip "GET /function/"
        const char *path_end = strchr(path_start, ' ');
        if (!path_end || (path_end - path_start) >= MAX_FUNC_NAME) {
            send_http(cfd, 400, "Bad Request", "{\"error\":\"invalid function name\"}", "application/json");
            return;
        }
        
        char func_name[MAX_FUNC_NAME];
        memcpy(func_name, path_start, (size_t)(path_end - path_start));
        func_name[path_end - path_start] = '\0';
        
        // Find function by name
        char func_id[MAX_FUNC_ID];
        if (find_function_by_name(func_name, func_id) < 0) {
            char resp[256];
            snprintf(resp, sizeof(resp), "{\"error\":\"function '%s' not found\"}", func_name);
            send_http(cfd, 404, "Not Found", resp, "application/json");
            return;
        }
        
        // Load function code
        char code_buf[MAX_BODY];
        int code_len = load_function(func_id, code_buf, sizeof(code_buf));
        if (code_len < 0) {
            send_http(cfd, 500, "Internal Error", "{\"error\":\"failed to load function code\"}", "application/json");
            return;
        }
        
        // Load metadata
        function_metadata_t meta;
        if (load_function_metadata(func_id, &meta) < 0) {
            send_http(cfd, 500, "Internal Error", "{\"error\":\"failed to load metadata\"}", "application/json");
            return;
        }
        
        // Build JSON response with code and metadata
        char *resp = (char*)malloc(code_len + 1024);
        if (!resp) {
            send_http(cfd, 500, "Internal Error", "{\"error\":\"malloc failed\"}", "application/json");
            return;
        }
        
        // Escape code for JSON (simple: replace " with ')
        char *escaped_code = (char*)malloc(code_len * 2 + 1);
        if (!escaped_code) {
            free(resp);
            send_http(cfd, 500, "Internal Error", "{\"error\":\"malloc failed\"}", "application/json");
            return;
        }
        
        int j = 0;
        for (int i = 0; i < code_len; i++) {
            if (code_buf[i] == '\"') {
                escaped_code[j++] = '\\';
                escaped_code[j++] = '\"';
            } else if (code_buf[i] == '\n') {
                escaped_code[j++] = '\\';
                escaped_code[j++] = 'n';
            } else if (code_buf[i] == '\r') {
                escaped_code[j++] = '\\';
                escaped_code[j++] = 'r';
            } else if (code_buf[i] == '\t') {
                escaped_code[j++] = '\\';
                escaped_code[j++] = 't';
            } else if (code_buf[i] == '\\') {
                escaped_code[j++] = '\\';
                escaped_code[j++] = '\\';
            } else {
                escaped_code[j++] = code_buf[i];
            }
        }
        escaped_code[j] = '\0';
        
        snprintf(resp, code_len + 1024,
            "{\"ok\":true,\"id\":\"%s\",\"name\":\"%s\",\"lang\":\"%s\",\"size\":%zu,\"code\":\"%s\"}",
            meta.id, meta.name, meta.language, meta.size, escaped_code);
        
        send_http(cfd, 200, "OK", resp, "application/json");
        free(escaped_code);
        free(resp);
        return;
    } else {
        const char *msg = "{\"error\":\"use POST /deploy or POST /invoke\"}";
        send_http(cfd, 404, "Not Found", msg, "application/json");
        return;
    }
}

static void handle_deploy(int cfd, const char *buf, ssize_t n) {
    // Support 2 formats:
    // 1. JSON: {"name":"x","lang":"c","code":"..."}  (small code)
    // 2. Query params + raw body: POST /deploy?name=x&lang=c  (large files)
    
    const char *hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end) {
        send_http(cfd, 400, "Bad Request", "{\"error\":\"bad headers\"}", "application/json");
        return;
    }

    int content_length = parse_content_length(buf);
    if (content_length <= 0 || content_length > MAX_BODY) {
        send_http(cfd, 400, "Bad Request", "{\"error\":\"invalid content length\"}", "application/json");
        return;
    }

    // Check if query params present (format 2: file upload)
    char name[MAX_FUNC_NAME] = {0};
    char lang[16] = {0};
    const char *query_start = strstr(buf, "POST /deploy?");
    if (query_start) {
        // Parse query params: ?name=xxx&lang=yyy
        const char *name_param = strstr(query_start, "name=");
        const char *lang_param = strstr(query_start, "lang=");
        if (name_param && lang_param) {
            sscanf(name_param, "name=%63[^& \r\n]", name);
            sscanf(lang_param, "lang=%15[^& \r\n]", lang);
        }
    }

    const char *body = hdr_end + 4;
    int already = (int)(n - (body - buf));
    char *payload = (char*)malloc((size_t)content_length + 1);
    if (!payload) {
        send_http(cfd, 500, "Internal Error", "{\"error\":\"malloc failed\"}", "application/json");
        return;
    }

    int copied = 0;
    if (already > 0) {
        int tocopy = already > content_length ? content_length : already;
        memcpy(payload, body, (size_t)tocopy);
        copied = tocopy;
    }
    while (copied < content_length) {
        ssize_t r = recv(cfd, payload + copied, (size_t)(content_length - copied), 0);
        if (r <= 0) { free(payload); send_http(cfd, 400, "Bad Request", "{\"error\":\"incomplete body\"}", "application/json"); return; }
        copied += (int)r;
    }
    payload[content_length] = '\0';

    char *code = NULL;
    size_t code_len = 0;

    // If name/lang from query, body is raw code (format 2)
    if (name[0] && lang[0]) {
        code = payload;
        code_len = (size_t)content_length;
    } else {
        // Format 1: JSON body
        const char *name_p = strstr(payload, "\"name\":");
        const char *lang_p = strstr(payload, "\"lang\":");
        const char *code_p = strstr(payload, "\"code\":");

        if (!name_p || !lang_p || !code_p) {
            send_http(cfd, 400, "Bad Request", "{\"error\":\"missing name, lang or code (use JSON or query params)\"}", "application/json");
            free(payload);
            return;
        }

        sscanf(name_p, "\"name\":\"%63[^\"]\"", name);
        sscanf(lang_p, "\"lang\":\"%15[^\"]\"", lang);

        // Extract code (between quotes after "code":")
        const char *code_start = strchr(code_p + 7, '\"');
        if (!code_start) {
            send_http(cfd, 400, "Bad Request", "{\"error\":\"invalid code format\"}", "application/json");
            free(payload);
            return;
        }
        code_start++;
        const char *code_end = code_start;
        while (*code_end && *code_end != '\"') code_end++;
        code_len = (size_t)(code_end - code_start);

        code = (char*)malloc(code_len + 1);
        if (!code) {
            send_http(cfd, 500, "Internal Error", "{\"error\":\"malloc failed\"}", "application/json");
            free(payload);
            return;
        }
        memcpy(code, code_start, code_len);
        code[code_len] = '\0';
    }

    // Send deploy request to Server (Server will compile and store)
    int sfd = create_unix_client_socket(SERVER_SOCK_PATH);
    if (sfd < 0) {
        send_http(cfd, 503, "Service Unavailable", "{\"error\":\"server down\"}", "application/json");
        if (code != payload) free(code);
        free(payload);
        return;
    }

    // Send deploy message to Server
    char msg[RECV_BUF];
    int msg_len = snprintf(msg, sizeof(msg), 
        "{\"type\":\"deploy\",\"name\":\"%s\",\"lang\":\"%s\",\"code\":\"", 
        name, lang);
    write_all(sfd, msg, (size_t)msg_len);
    
    // Send code (escape quotes)
    for (size_t i = 0; i < code_len; i++) {
        char ch = code[i];
        if (ch == '"' || ch == '\\') {
            write_all(sfd, "\\", 1);
        } else if (ch == '\n') {
            write_all(sfd, "\\n", 2);
            continue;
        } else if (ch == '\r') {
            write_all(sfd, "\\r", 2);
            continue;
        } else if (ch == '\t') {
            write_all(sfd, "\\t", 2);
            continue;
        }
        write_all(sfd, &ch, 1);
    }
    write_all(sfd, "\"}\n", 3);

    // Read response from Server
    char resp[RECV_BUF];
    ssize_t rl = read_line(sfd, resp, sizeof(resp));
    close(sfd);
    
    if (rl <= 0) {
        send_http(cfd, 502, "Bad Gateway", "{\"error\":\"no response from server\"}", "application/json");
        if (code != payload) free(code);
        free(payload);
        return;
    }

    // Forward Server's response to client
    send_http(cfd, 201, "Created", resp, "application/json");

    if (code != payload) free(code);
    free(payload);
}

static void handle_invoke(int cfd, const char *buf, ssize_t n) {

    // Extract function name from query (?fn=...)
    const char *q = strchr(buf, ' ');
    const char *path_start = q ? q+1 : NULL;
    const char *path_end = path_start ? strchr(path_start, ' ') : NULL;
    char fn[128] = {0};
    if (path_start && path_end && (path_end - path_start) < 256) {
        char path[256];
        memcpy(path, path_start, (size_t)(path_end - path_start));
        path[path_end - path_start] = '\0';
        const char *fnk = strstr(path, "?fn=");
        if (fnk) {
            fnk += 4;
            strncpy(fn, fnk, sizeof(fn)-1);
        } else {
            strncpy(fn, "echo", sizeof(fn)-1);
        }
    } else {
        strncpy(fn, "echo", sizeof(fn)-1);
    }

    const char *hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end) {
        send_http(cfd, 400, "Bad Request", "{\"error\":\"bad headers\"}", "application/json");
        return;
    }

    int content_length = parse_content_length(buf);
    const char *body = hdr_end + 4;
    int already = (int)(n - (body - buf));
    char *payload = NULL;
    if (content_length > 0) {
        payload = (char*)malloc((size_t)content_length + 1);
        if (!payload) die("malloc");
        int copied = 0;
        if (already > 0) {
            int tocopy = already > content_length ? content_length : already;
            memcpy(payload, body, (size_t)tocopy);
            copied = tocopy;
        }
        while (copied < content_length) {
            ssize_t r = recv(cfd, payload + copied, (size_t)(content_length - copied), 0);
            if (r <= 0) { free(payload); payload = NULL; break; }
            copied += (int)r;
        }
        if (payload) payload[content_length] = '\0';
    } else {
        payload = strdup("");
    }

    // Send invoke to Server via UNIX socket
    int sfd = create_unix_client_socket(SERVER_SOCK_PATH);
    if (sfd < 0) {
        send_http(cfd, 503, "Service Unavailable", "{\"error\":\"server down\"}", "application/json");
        free(payload);
        return;
    }

    char line[RECV_BUF];
    int m = snprintf(line, sizeof(line), "{\"type\":\"invoke\",\"fn\":\"%s\",\"payload\":\"", fn);
    write_all(sfd, line, (size_t)m);
    // naive escape: replace quotes with single quotes
    for (int i=0; payload && payload[i]; i++) {
        char ch = payload[i];
        if (ch == '\"') ch = '\'';
        write_all(sfd, &ch, 1);
    }
    write_all(sfd, "\"}\n", 4);

    // read response line
    char resp[RECV_BUF];
    ssize_t rl = read_line(sfd, resp, sizeof(resp));
    if (rl <= 0) {
        send_http(cfd, 502, "Bad Gateway", "{\"error\":\"no resp\"}", "application/json");
        close(sfd);
        free(payload);
        return;
    }

    send_http(cfd, 200, "OK", resp, "application/json");
    close(sfd);
    free(payload);
}

int api_gateway_main(void) {
    int sfd = start_http_server();
    printf("api_gateway listening on 127.0.0.1:%d\n", HTTP_PORT);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int cfd = accept(sfd, (struct sockaddr*)&cli, &clilen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        handle_client(cfd);
        close(cfd);
    }
}
