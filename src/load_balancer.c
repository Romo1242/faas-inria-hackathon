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

#include "ipc.h"

#define MAX_WORKERS 32
#define LINE_MAX 8192

typedef enum {
    STRATEGY_RR,    // Round Robin
    STRATEGY_FIFO,  // First In First Out
    STRATEGY_WEIGHTED // Weighted (TODO)
} lb_strategy_t;

typedef struct {
    int worker_id;  // Worker ID from server
    pid_t pid;
    int active;
    int load;       // current load (for weighted strategy)
    int registered; // Has been registered by server
} worker_info_t;

static worker_info_t workers[MAX_WORKERS];
static int num_active_workers = 0;
static lb_strategy_t strategy = STRATEGY_RR;
static int rr_idx = 0;
static volatile sig_atomic_t running = 1;

// Communication with server for worker access
static int server_fd = -1;

static void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i].registered && workers[i].pid == pid) {
                fprintf(stderr, "lb: worker %d (pid %d) exited\n", workers[i].worker_id, pid);
                workers[i].active = 0;
                workers[i].registered = 0;
                workers[i].pid = 0;
                num_active_workers--;
            }
        }
    }
}

static void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

// Register a worker when server notifies us
static void register_worker(int worker_id, pid_t worker_pid) {
    if (worker_id < 0 || worker_id >= MAX_WORKERS) {
        fprintf(stderr, "lb: invalid worker_id %d\n", worker_id);
        return;
    }
    
    workers[worker_id].worker_id = worker_id;
    workers[worker_id].pid = worker_pid;
    workers[worker_id].active = 1;
    workers[worker_id].registered = 1;
    workers[worker_id].load = 0;
    num_active_workers++;
    
    fprintf(stderr, "lb: registered worker %d (pid %d)\n", worker_id, worker_pid);
}


static int pick_worker_rr(void) {
    // Round Robin
    if (num_active_workers == 0) return -1;
    for (int i = 0; i < MAX_WORKERS; i++) {
        int idx = (rr_idx + i) % MAX_WORKERS;
        if (workers[idx].active) {
            rr_idx = (idx + 1) % MAX_WORKERS;
            return idx;
        }
    }
    return -1;
}

static int pick_worker_fifo(void) {
    // FIFO: always pick first available worker
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i].active) {
            return i;
        }
    }
    return -1;
}

static int pick_worker(void) {
    switch (strategy) {
        case STRATEGY_RR:
            return pick_worker_rr();
        case STRATEGY_FIFO:
            return pick_worker_fifo();
        case STRATEGY_WEIGHTED:
            // TODO: pick worker with lowest load
            return pick_worker_rr();
        default:
            return pick_worker_rr();
    }
}

static void handle_invoke(int client_fd, const char *line) {
    fprintf(stderr, "[LB] 📨 Received invoke request: %s", line);
    
    int widx = pick_worker();
    if (widx < 0) {
        fprintf(stderr, "[LB] ❌ No worker available\n");
        const char *resp = "{\"ok\":false,\"error\":\"no worker available\"}\n";
        write_all(client_fd, resp, strlen(resp));
        close(client_fd);
        return;
    }

    fprintf(stderr, "[LB] ✅ Selected worker %d (PID %d)\n", workers[widx].worker_id, workers[widx].pid);

    // Extract fn and payload from invoke line
    const char *fnp = strstr(line, "\"fn\":");
    const char *plp = strstr(line, "\"payload\":");
    if (!fnp || !plp) {
        fprintf(stderr, "[LB] ❌ Bad invoke format (missing fn or payload)\n");
        const char *resp = "{\"ok\":false,\"error\":\"bad invoke format\"}\n";
        write_all(client_fd, resp, strlen(resp));
        close(client_fd);
        return;
    }

    // Parse fn and payload values
    char func_id[256] = {0};
    char payload[LINE_MAX] = {0};
    sscanf(fnp, "\"fn\":\"%255[^\"]\"", func_id);
    sscanf(plp, "\"payload\":\"%8191[^\"]\"", payload);

    // Build job line for worker
    char job[LINE_MAX];
    snprintf(job, sizeof(job), "{\"type\":\"job\",\"fn\":\"%s\",\"payload\":\"%s\"}\n", 
             func_id, payload);

    fprintf(stderr, "[LB] 📦 Built job: %s", job);

    // Send job to worker via server
    // We need to ask server to forward the job to the specific worker
    fprintf(stderr, "[LB] 🔗 Connecting to Server...\n");
    int srv_fd = create_unix_client_socket(SERVER_SOCK_PATH);
    if (srv_fd < 0) {
        fprintf(stderr, "[LB] ❌ Server unavailable\n");
        const char *resp = "{\"ok\":false,\"error\":\"server unavailable\"}\n";
        write_all(client_fd, resp, strlen(resp));
        close(client_fd);
        return;
    }

    fprintf(stderr, "[LB] ✅ Connected to Server\n");

    // Send job request to server with worker ID
    char forward_msg[LINE_MAX];
    snprintf(forward_msg, sizeof(forward_msg), 
        "{\"type\":\"forward_to_worker\",\"worker_id\":%d,\"job\":%s}\n",
        workers[widx].worker_id, job);
    
    fprintf(stderr, "[LB] 📤 Sending to Server: %s", forward_msg);
    write_all(srv_fd, forward_msg, strlen(forward_msg));

    // Read response from server (which got it from worker)
    fprintf(stderr, "[LB] ⏳ Waiting for response from Server...\n");
    char resp[LINE_MAX];
    ssize_t n = read_line(srv_fd, resp, sizeof(resp));
    if (n <= 0) {
        fprintf(stderr, "[LB] ❌ Worker timeout (no response)\n");
        const char *err = "{\"ok\":false,\"error\":\"worker timeout\"}\n";
        write_all(client_fd, err, strlen(err));
    } else {
        fprintf(stderr, "[LB] ✅ Received response from Server: %s", resp);
        write_all(client_fd, resp, (size_t)n);
    }

    workers[widx].load++;
    close(srv_fd);
    close(client_fd);
    fprintf(stderr, "[LB] 🏁 Request completed\n");
}

int load_balancer_main(int argc, char **argv) {
    // Parse args: strategy only (workers created by Server)
    if (argc > 1) {
        if (strcmp(argv[1], "FIFO") == 0) strategy = STRATEGY_FIFO;
        else if (strcmp(argv[1], "WEIGHTED") == 0) strategy = STRATEGY_WEIGHTED;
        else strategy = STRATEGY_RR;
    }

    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);

    // Initialize workers array
    for (int i = 0; i < MAX_WORKERS; i++) {
        workers[i].active = 0;
        workers[i].registered = 0;
        workers[i].worker_id = -1;
        workers[i].pid = 0;
        workers[i].load = 0;
    }

    int lfd = create_unix_server_socket(LB_SOCK_PATH);
    const char *strat_name = (strategy == STRATEGY_RR) ? "RR" : 
                             (strategy == STRATEGY_FIFO) ? "FIFO" : "WEIGHTED";
    fprintf(stderr, "load_balancer: listening on %s (%s strategy)\n", 
            LB_SOCK_PATH, strat_name);
    fprintf(stderr, "load_balancer: waiting for worker registrations from server...\n");

    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        struct timeval tv = {1, 0};
        int r = select(lfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (r == 0) continue; // timeout

        if (FD_ISSET(lfd, &rfds)) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd < 0) {
                if (errno == EINTR) continue;
                perror("accept");
                continue;
            }

            char line[LINE_MAX];
            ssize_t n = read_line(cfd, line, sizeof(line));
            if (n <= 0) {
                close(cfd);
                continue;
            }

            if (strstr(line, "\"type\":\"register_worker\"")) {
                // Server is registering a worker
                int worker_id = -1;
                pid_t pid = 0;
                sscanf(line, "{\"type\":\"register_worker\",\"worker_id\":%d,\"pid\":%d}", 
                       &worker_id, &pid);
                if (worker_id >= 0 && pid > 0) {
                    register_worker(worker_id, pid);
                }
                close(cfd);
            } else if (strstr(line, "\"type\":\"invoke\"")) {
                handle_invoke(cfd, line);
            } else {
                const char *resp = "{\"ok\":false,\"error\":\"unknown msg\"}\n";
                write_all(cfd, resp, strlen(resp));
                close(cfd);
            }
        }
    }

    // Cleanup
    fprintf(stderr, "load_balancer: shutting down...\n");
    unlink(LB_SOCK_PATH);
    fprintf(stderr, "load_balancer: shutdown complete\n");
    return 0;
}
