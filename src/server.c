#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "ipc.h"
#include "storage.h"

#define LINE_MAX 8192
#define MAX_WORKERS 32
#define WORKER_POOL_SIZE 4  // Pre-fork this many workers at startup

typedef struct {
    pid_t pid;
    int pipe_to_worker[2];   // Pipe to send jobs to worker
    int pipe_from_worker[2]; // Pipe to receive results from worker
    int active;
} worker_info_t;

static worker_info_t workers[MAX_WORKERS];
static int num_workers = 0;
static volatile sig_atomic_t running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

static void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Worker terminated
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i].active && workers[i].pid == pid) {
                fprintf(stderr, "server: worker %d (pid %d) exited with status %d\n", i, pid, WEXITSTATUS(status));
                close(workers[i].pipe_to_worker[1]);
                close(workers[i].pipe_from_worker[0]);
                workers[i].active = 0;
                workers[i].pid = 0;
                num_workers--;
            }
        }
    }
}

// Create a worker process via fork()
static int create_worker(void) {
    if (num_workers >= MAX_WORKERS) {
        fprintf(stderr, "server: max workers reached\n");
        return -1;
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (!workers[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    // Create pipes for bidirectional communication
    int pipe_to[2], pipe_from[2];
    if (pipe(pipe_to) < 0 || pipe(pipe_from) < 0) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork worker");
        close(pipe_to[0]); close(pipe_to[1]);
        close(pipe_from[0]); close(pipe_from[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process: Worker
        close(pipe_to[1]);   // Close write end of input pipe
        close(pipe_from[0]); // Close read end of output pipe

        // Redirect stdin to pipe_to[0] and stdout to pipe_from[1]
        dup2(pipe_to[0], STDIN_FILENO);
        dup2(pipe_from[1], STDOUT_FILENO);
        
        close(pipe_to[0]);
        close(pipe_from[1]);

        // Set worker ID environment variable
        char worker_id[32];
        snprintf(worker_id, sizeof(worker_id), "%d", slot);
        setenv("WORKER_ID", worker_id, 1);

        // Execute worker binary
        execl("./build/bin/worker", "worker", NULL);
        perror("execl worker");
        exit(1);
    }

    // Parent process: Server
    close(pipe_to[0]);   // Close read end of input pipe
    close(pipe_from[1]); // Close write end of output pipe

    workers[slot].pid = pid;
    workers[slot].pipe_to_worker[0] = -1;
    workers[slot].pipe_to_worker[1] = pipe_to[1];
    workers[slot].pipe_from_worker[0] = pipe_from[0];
    workers[slot].pipe_from_worker[1] = -1;
    workers[slot].active = 1;
    num_workers++;

    fprintf(stderr, "server: created worker %d (pid %d)\n", slot, pid);
    
    // Register worker with Load Balancer
    int lb_fd = create_unix_client_socket(LB_SOCK_PATH);
    if (lb_fd >= 0) {
        char reg_msg[256];
        snprintf(reg_msg, sizeof(reg_msg), 
            "{\"type\":\"register_worker\",\"worker_id\":%d,\"pid\":%d}\n", 
            slot, pid);
        write_all(lb_fd, reg_msg, strlen(reg_msg));
        close(lb_fd);
        fprintf(stderr, "server: registered worker %d with load balancer\n", slot);
    } else {
        fprintf(stderr, "server: warning - could not register worker with load balancer\n");
    }

    return slot;
}

// Compile code to WASM based on language
static int compile_to_wasm(const char *func_id, const char *lang, const char *code_path, char *wasm_path, size_t wasm_path_len) {
    snprintf(wasm_path, wasm_path_len, "%s/%s/code.wasm", FUNCTIONS_DIR, func_id);
    
    // Check if already compiled
    if (access(wasm_path, F_OK) == 0) {
        fprintf(stderr, "[SERVER] WASM already exists: %s\n", wasm_path);
        return 0;
    }
    
    char cmd[2048];
    if (strcmp(lang, "c") == 0) {
        // Compile C to WASM using clang with wasi-sdk (FULL PATH)
        snprintf(cmd, sizeof(cmd), 
            "/opt/wasi-sdk/bin/clang --target=wasm32-wasi -Wl,--export-all -o %s %s 2>&1",
            wasm_path, code_path);
    } else if (strcmp(lang, "rust") == 0 || strcmp(lang, "rs") == 0) {
        // Compile Rust to WASM (FULL PATH if needed)
        snprintf(cmd, sizeof(cmd), 
            "rustc --target wasm32-wasi -o %s %s 2>&1", 
            wasm_path, code_path);
    } else if (strcmp(lang, "go") == 0) {
        // Compile Go to WASM using TinyGo (FULL PATH if needed)
        snprintf(cmd, sizeof(cmd), 
            "tinygo build -target=wasi -o %s %s 2>&1", 
            wasm_path, code_path);
    } else if (strcmp(lang, "python") == 0 || strcmp(lang, "py") == 0) {
        fprintf(stderr, "[SERVER] Python will be executed with python3 runtime (no WASM)\n");
        return 0;
    } else if (strcmp(lang, "js") == 0 || strcmp(lang, "javascript") == 0) {
        fprintf(stderr, "[SERVER] JavaScript will be executed with node runtime (no WASM)\n");
        return 0;
    } else if (strcmp(lang, "php") == 0) {
        fprintf(stderr, "[SERVER] PHP will be executed with php runtime (no WASM)\n");
        return 0;
    } else if (strcmp(lang, "html") == 0) {
        fprintf(stderr, "[SERVER] HTML stored as static file (no compilation)\n");
        return 0;
    } else {
        fprintf(stderr, "[SERVER] Unsupported language for WASM: %s\n", lang);
        return -1;
    }
    
    fprintf(stderr, "[SERVER] 🔨 Compiling %s to WASM...\n", lang);
    fprintf(stderr, "[SERVER] Command: %s\n", cmd);
    
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[SERVER] ❌ Compilation failed (exit code %d)\n", ret);
        return -1;
    }
    
    fprintf(stderr, "[SERVER] ✅ Compilation successful: %s\n", wasm_path);
    return 0;
}

// Handle deploy request
static void handle_deploy(int client_fd, const char *line) {
    // Parse: {"type":"deploy","name":"xxx","lang":"yyy","code":"..."}
    char name[MAX_FUNC_NAME] = {0};
    char lang[16] = {0};
    
    const char *name_p = strstr(line, "\"name\":\"");
    const char *lang_p = strstr(line, "\"lang\":\"");
    const char *code_p = strstr(line, "\"code\":\"");
    
    if (!name_p || !lang_p || !code_p) {
        const char *resp = "{\"ok\":false,\"error\":\"missing name, lang or code\"}\n";
        write_all(client_fd, resp, strlen(resp));
        return;
    }
    
    sscanf(name_p, "\"name\":\"%63[^\"]\"", name);
    sscanf(lang_p, "\"lang\":\"%15[^\"]\"", lang);
    
    // Extract code (between quotes, handle escapes)
    const char *code_start = code_p + 8; // Skip "code":"
    const char *code_end = code_start;
    size_t code_len = 0;
    
    // Find end of code string (handle escaped quotes)
    while (*code_end) {
        if (*code_end == '\\' && *(code_end + 1)) {
            code_end += 2;
            code_len += 2;
        } else if (*code_end == '\"') {
            break;
        } else {
            code_end++;
            code_len++;
        }
    }
    
    char *code = (char*)malloc(code_len + 1);
    if (!code) {
        const char *resp = "{\"ok\":false,\"error\":\"malloc failed\"}\n";
        write_all(client_fd, resp, strlen(resp));
        return;
    }
    
    // Unescape code
    size_t j = 0;
    for (size_t i = 0; i < code_len; i++) {
        if (code_start[i] == '\\' && i + 1 < code_len) {
            char next = code_start[i + 1];
            if (next == 'n') { code[j++] = '\n'; i++; }
            else if (next == 'r') { code[j++] = '\r'; i++; }
            else if (next == 't') { code[j++] = '\t'; i++; }
            else if (next == '\"') { code[j++] = '\"'; i++; }
            else if (next == '\\') { code[j++] = '\\'; i++; }
            else { code[j++] = code_start[i]; }
        } else {
            code[j++] = code_start[i];
        }
    }
    code[j] = '\0';
    code_len = j;
    
    fprintf(stderr, "[SERVER] 📥 Deploy request: name=%s, lang=%s, code_len=%zu\n", name, lang, code_len);
    
    // Check if function name already exists
    if (function_name_exists(name)) {
        char resp[512];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"function '%s' already exists\"}\n", name);
        write_all(client_fd, resp, strlen(resp));
        free(code);
        return;
    }
    
    // Store function
    char func_id[MAX_FUNC_ID];
    if (store_function(name, lang, code, code_len, func_id) < 0) {
        const char *resp = "{\"ok\":false,\"error\":\"failed to store function\"}\n";
        write_all(client_fd, resp, strlen(resp));
        free(code);
        return;
    }
    
    fprintf(stderr, "[SERVER] 💾 Function stored: %s\n", func_id);
    
    // Compile to WASM if applicable
    char code_path[512];
    const char *ext = (strcmp(lang, "c") == 0) ? "c" :
                      (strcmp(lang, "js") == 0 || strcmp(lang, "javascript") == 0) ? "js" :
                      (strcmp(lang, "python") == 0 || strcmp(lang, "py") == 0) ? "py" :
                      (strcmp(lang, "rust") == 0 || strcmp(lang, "rs") == 0) ? "rs" :
                      (strcmp(lang, "go") == 0) ? "go" :
                      (strcmp(lang, "php") == 0) ? "php" :
                      (strcmp(lang, "html") == 0) ? "html" : "txt";
    snprintf(code_path, sizeof(code_path), "%s/%s/code.%s", FUNCTIONS_DIR, func_id, ext);
    
    char wasm_path[512];
    int compile_result = compile_to_wasm(func_id, lang, code_path, wasm_path, sizeof(wasm_path));
    
    // Return success with function ID
    char resp[512];
    if (compile_result == 0 && access(wasm_path, F_OK) == 0) {
        snprintf(resp, sizeof(resp), 
            "{\"ok\":true,\"id\":\"%s\",\"name\":\"%s\",\"lang\":\"%s\",\"wasm\":true}\n", 
            func_id, name, lang);
    } else {
        snprintf(resp, sizeof(resp), 
            "{\"ok\":true,\"id\":\"%s\",\"name\":\"%s\",\"lang\":\"%s\",\"wasm\":false}\n", 
            func_id, name, lang);
    }
    
    write_all(client_fd, resp, strlen(resp));
    free(code);
}

static void handle_request(int client_fd) {
    char line[LINE_MAX];
    ssize_t n = read_line(client_fd, line, sizeof(line));
    if (n <= 0) {
        close(client_fd);
        return;
    }

    // Check if this is a deploy request from API Gateway
    if (strstr(line, "\"type\":\"deploy\"")) {
        handle_deploy(client_fd, line);
        close(client_fd);
        return;
    }

    // Check if this is a forward_to_worker request from LB
    if (strstr(line, "\"type\":\"forward_to_worker\"")) {
        fprintf(stderr, "[SERVER] 📨 Received forward_to_worker from LB: %s", line);
        
        // Extract worker_id and job
        int worker_id = -1;
        const char *job_start = strstr(line, "\"job\":");
        if (!job_start) {
            fprintf(stderr, "[SERVER] ❌ Invalid forward request (no job field)\n");
            const char *resp = "{\"ok\":false,\"error\":\"invalid forward request\"}\n";
            write_all(client_fd, resp, strlen(resp));
            close(client_fd);
            return;
        }
        
        sscanf(line, "{\"type\":\"forward_to_worker\",\"worker_id\":%d", &worker_id);
        fprintf(stderr, "[SERVER] 🎯 Target worker: %d\n", worker_id);
        
        if (worker_id < 0 || worker_id >= MAX_WORKERS || !workers[worker_id].active) {
            fprintf(stderr, "[SERVER] ❌ Invalid worker %d (active=%d)\n", worker_id, 
                    (worker_id >= 0 && worker_id < MAX_WORKERS) ? workers[worker_id].active : -1);
            const char *resp = "{\"ok\":false,\"error\":\"invalid worker\"}\n";
            write_all(client_fd, resp, strlen(resp));
            close(client_fd);
            return;
        }
        
        // Extract job JSON (skip "job": and get the object)
        job_start += 6; // Skip "job":
        while (*job_start == ' ') job_start++;
        
        fprintf(stderr, "[SERVER] 📤 Sending job to worker %d via pipe: %s", worker_id, job_start);
        
        // Send job to worker via pipe
        write_all(workers[worker_id].pipe_to_worker[1], job_start, strlen(job_start));
        
        fprintf(stderr, "[SERVER] ⏳ Waiting for response from worker %d...\n", worker_id);
        
        // Read response from worker
        char resp[LINE_MAX];
        ssize_t rl = read_line(workers[worker_id].pipe_from_worker[0], resp, sizeof(resp));
        if (rl <= 0) {
            fprintf(stderr, "[SERVER] ❌ Worker %d timeout (no response)\n", worker_id);
            const char *err = "{\"ok\":false,\"error\":\"worker timeout\"}\n";
            write_all(client_fd, err, strlen(err));
        } else {
            fprintf(stderr, "[SERVER] ✅ Received response from worker %d: %s", worker_id, resp);
            write_all(client_fd, resp, (size_t)rl);
        }
        
        close(client_fd);
        fprintf(stderr, "[SERVER] 🏁 forward_to_worker completed\n");
        return;
    }

    // Otherwise, forward request to Load Balancer (for invoke requests)
    fprintf(stderr, "[SERVER] 🔄 Forwarding to Load Balancer: %s", line);
    
    int lb_fd = create_unix_client_socket(LB_SOCK_PATH);
    if (lb_fd < 0) {
        fprintf(stderr, "[SERVER] ❌ Load Balancer unavailable\n");
        const char *resp = "{\"ok\":false,\"error\":\"load balancer unavailable\"}\n";
        write_all(client_fd, resp, strlen(resp));
        close(client_fd);
        return;
    }

    fprintf(stderr, "[SERVER] ✅ Connected to Load Balancer\n");

    // Send request to LB
    fprintf(stderr, "[SERVER] 📤 Sending to LB: %s", line);
    write_all(lb_fd, line, strlen(line));

    // Read response from LB
    fprintf(stderr, "[SERVER] ⏳ Waiting for response from LB...\n");
    char resp[LINE_MAX];
    ssize_t rl = read_line(lb_fd, resp, sizeof(resp));
    if (rl <= 0) {
        fprintf(stderr, "[SERVER] ❌ LB timeout (no response)\n");
        const char *err = "{\"ok\":false,\"error\":\"lb timeout\"}\n";
        write_all(client_fd, err, strlen(err));
    } else {
        fprintf(stderr, "[SERVER] ✅ Received response from LB: %s", resp);
        write_all(client_fd, resp, (size_t)rl);
    }

    close(lb_fd);
    close(client_fd);
    fprintf(stderr, "[SERVER] 🏁 Request completed\n");
}

// Thread wrapper for handle_request
static void* handle_request_thread(void *arg) {
    int cfd = *(int*)arg;
    free(arg);
    handle_request(cfd);
    return NULL;
}

int server_main(void) {
    signal(SIGINT, sigint_handler);
    signal(SIGCHLD, sigchld_handler);

    // Initialize workers array
    for (int i = 0; i < MAX_WORKERS; i++) {
        workers[i].active = 0;
        workers[i].pid = 0;
    }

    int sfd = create_unix_server_socket(SERVER_SOCK_PATH);
    fprintf(stderr, "server: listening on %s\n", SERVER_SOCK_PATH);
    fprintf(stderr, "server: pre-forking %d workers...\n", WORKER_POOL_SIZE);

    // Pre-fork worker pool
    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        if (create_worker() < 0) {
            fprintf(stderr, "server: failed to create worker %d\n", i);
        }
        usleep(50000); // 50ms delay between worker creation
    }

    fprintf(stderr, "server: %d workers ready, forwarding requests to load balancer\n", num_workers);

    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sfd, &rfds);
        struct timeval tv = {1, 0};
        int r = select(sfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (r == 0) continue; // timeout

        if (FD_ISSET(sfd, &rfds)) {
            int cfd = accept(sfd, NULL, NULL);
            if (cfd < 0) {
                if (errno == EINTR) continue;
                perror("accept");
                continue;
            }
            
            // Handle request in separate thread to avoid blocking
            int *cfd_ptr = malloc(sizeof(int));
            *cfd_ptr = cfd;
            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_request_thread, cfd_ptr) != 0) {
                perror("pthread_create");
                close(cfd);
                free(cfd_ptr);
            } else {
                pthread_detach(thread);  // Auto-cleanup when thread finishes
            }
        }
    }

    // Cleanup: kill all workers
    fprintf(stderr, "server: shutting down, killing %d workers...\n", num_workers);
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i].active) {
            kill(workers[i].pid, SIGTERM);
            close(workers[i].pipe_to_worker[1]);
            close(workers[i].pipe_from_worker[0]);
        }
    }
    
    unlink(SERVER_SOCK_PATH);
    fprintf(stderr, "server: shutdown complete\n");
    return 0;
}
