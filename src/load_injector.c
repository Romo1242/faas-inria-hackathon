#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <pthread.h>

#define SERVER_SOCK_PATH "/tmp/faas_server.sock"
#define LINE_MAX 8192

typedef struct {
    int thread_id;
    int num_requests;
    const char *function_id;
    int *success_count;
    int *error_count;
    pthread_mutex_t *mutex;
} thread_args_t;

static int connect_to_server(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SERVER_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

static ssize_t read_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return n;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

static void* worker_thread(void *arg) {
    thread_args_t *args = (thread_args_t*)arg;
    
    for (int i = 0; i < args->num_requests; i++) {
        int fd = connect_to_server();
        if (fd < 0) {
            pthread_mutex_lock(args->mutex);
            (*args->error_count)++;
            pthread_mutex_unlock(args->mutex);
            continue;
        }

        // Send invoke request
        char req[LINE_MAX];
        snprintf(req, sizeof(req), 
            "{\"type\":\"invoke\",\"fn\":\"%s\",\"payload\":\"test from thread %d req %d\"}\n",
            args->function_id, args->thread_id, i);
        
        if (write(fd, req, strlen(req)) < 0) {
            pthread_mutex_lock(args->mutex);
            (*args->error_count)++;
            pthread_mutex_unlock(args->mutex);
            close(fd);
            continue;
        }

        // Read response
        char resp[LINE_MAX];
        ssize_t n = read_line(fd, resp, sizeof(resp));
        if (n > 0 && strstr(resp, "\"ok\":true")) {
            pthread_mutex_lock(args->mutex);
            (*args->success_count)++;
            pthread_mutex_unlock(args->mutex);
        } else {
            pthread_mutex_lock(args->mutex);
            (*args->error_count)++;
            pthread_mutex_unlock(args->mutex);
        }

        close(fd);
        
        // Small delay between requests
        usleep(10000); // 10ms
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <function_id> <num_threads> <requests_per_thread>\n", argv[0]);
        fprintf(stderr, "Example: %s hello_1234567890 10 100\n", argv[0]);
        return 1;
    }

    const char *function_id = argv[1];
    int num_threads = atoi(argv[2]);
    int requests_per_thread = atoi(argv[3]);

    if (num_threads <= 0 || num_threads > 100) {
        fprintf(stderr, "Error: num_threads must be between 1 and 100\n");
        return 1;
    }

    if (requests_per_thread <= 0 || requests_per_thread > 10000) {
        fprintf(stderr, "Error: requests_per_thread must be between 1 and 10000\n");
        return 1;
    }

    printf("=== Load Injector ===\n");
    printf("Function ID: %s\n", function_id);
    printf("Threads: %d\n", num_threads);
    printf("Requests per thread: %d\n", requests_per_thread);
    printf("Total requests: %d\n", num_threads * requests_per_thread);
    printf("=====================\n\n");

    int success_count = 0;
    int error_count = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    thread_args_t *args = malloc(sizeof(thread_args_t) * num_threads);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Create threads
    for (int i = 0; i < num_threads; i++) {
        args[i].thread_id = i;
        args[i].num_requests = requests_per_thread;
        args[i].function_id = function_id;
        args[i].success_count = &success_count;
        args[i].error_count = &error_count;
        args[i].mutex = &mutex;

        if (pthread_create(&threads[i], NULL, worker_thread, &args[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    // Wait for all threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    int total = success_count + error_count;
    double rps = total / elapsed;

    printf("\n=== Results ===\n");
    printf("Total requests: %d\n", total);
    printf("Successful: %d (%.1f%%)\n", success_count, 100.0 * success_count / total);
    printf("Errors: %d (%.1f%%)\n", error_count, 100.0 * error_count / total);
    printf("Elapsed time: %.2f seconds\n", elapsed);
    printf("Requests/sec: %.2f\n", rps);
    printf("===============\n");

    free(threads);
    free(args);
    pthread_mutex_destroy(&mutex);

    return 0;
}
