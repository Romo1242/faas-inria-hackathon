/*
** Mini FaaS - Main Server
** Unified binary that starts:
** 1. Load Balancer (UNIX socket)
** 2. API Gateway (HTTP server)
** 3. Workers (fork × 4 with Wasmer initialized)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <pthread.h>

// Forward declarations of component main functions
extern int load_balancer_main(int argc, char **argv);
extern int api_gateway_main(void);
extern int server_main(void);

static volatile sig_atomic_t running = 1;
static pid_t lb_pid = 0;
static pid_t api_pid = 0;
static pid_t server_pid = 0;

// Signal handler for graceful shutdown
static void sigint_handler(int sig) {
    (void)sig;
    printf("\n[MAIN] 🛑 Arrêt du système...\n");
    running = 0;
    
    // Kill all child processes
    if (server_pid > 0) {
        printf("[MAIN] Arrêt du Server (PID %d)...\n", server_pid);
        kill(server_pid, SIGTERM);
    }
    if (api_pid > 0) {
        printf("[MAIN] Arrêt de l'API Gateway (PID %d)...\n", api_pid);
        kill(api_pid, SIGTERM);
    }
    if (lb_pid > 0) {
        printf("[MAIN] Arrêt du Load Balancer (PID %d)...\n", lb_pid);
        kill(lb_pid, SIGTERM);
    }
}

// Thread function for Load Balancer
static void* run_load_balancer(void *arg) {
    char *strategy = (char*)arg;
    char *argv[] = {"load_balancer", strategy, NULL};
    
    printf("[LB] 🔄 Démarrage du Load Balancer (stratégie: %s)...\n", strategy);
    load_balancer_main(2, argv);
    
    return NULL;
}

// Thread function for API Gateway
static void* run_api_gateway(void *arg) {
    (void)arg;
    
    printf("[API] 🌐 Démarrage de l'API Gateway (port 8080)...\n");
    api_gateway_main();
    
    return NULL;
}

int main(int argc, char **argv) {
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║         🚀 Mini FaaS - Unified Server 🚀            ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Get load balancing strategy from args (default: RR)
    char *strategy = "RR";
    if (argc > 1) {
        strategy = argv[1];
    }
    
    printf("[MAIN] Stratégie de load balancing: %s\n", strategy);
    printf("[MAIN] PID principal: %d\n", getpid());
    printf("\n");
    
    // Setup signal handlers
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    
    // Start Load Balancer in separate thread
    pthread_t lb_thread;
    if (pthread_create(&lb_thread, NULL, run_load_balancer, strategy) != 0) {
        perror("pthread_create load_balancer");
        return 1;
    }
    pthread_detach(lb_thread);
    sleep(1); // Give LB time to start
    
    // Start API Gateway in separate thread
    pthread_t api_thread;
    if (pthread_create(&api_thread, NULL, run_api_gateway, NULL) != 0) {
        perror("pthread_create api_gateway");
        return 1;
    }
    pthread_detach(api_thread);
    sleep(1); // Give API time to start
    
    // Start Server (which will fork workers)
    printf("[SERVER] 👷 Démarrage du Server et création des workers...\n");
    server_pid = fork();
    
    if (server_pid < 0) {
        perror("fork server");
        return 1;
    }
    
    if (server_pid == 0) {
        // Child process: run server
        server_main();
        exit(0);
    }
    
    // Parent process: wait and monitor
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║              ✅ Système Démarré !                    ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("📊 Composants actifs:\n");
    printf("  • Load Balancer:  /tmp/faas_lb.sock\n");
    printf("  • API Gateway:    http://127.0.0.1:8080\n");
    printf("  • Server:         PID %d (+ 4 workers)\n", server_pid);
    printf("\n");
    printf("🧪 Pour tester:\n");
    printf("  curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \\\n");
    printf("    -H \"Content-Type: text/plain\" --data-binary @examples/hello.c\n");
    printf("\n");
    printf("  curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_<timestamp>'\n");
    printf("\n");
    printf("⏹️  Appuyez sur Ctrl+C pour arrêter...\n");
    printf("\n");
    
    // Wait for child processes
    while (running) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        
        if (pid > 0) {
            if (pid == server_pid) {
                printf("[MAIN] ⚠️  Server terminé (PID %d, status %d)\n", pid, WEXITSTATUS(status));
                server_pid = 0;
                running = 0;
            }
        }
        
        sleep(1);
    }
    
    // Cleanup
    printf("[MAIN] 🧹 Nettoyage...\n");
    
    // Wait for all children
    while (waitpid(-1, NULL, WNOHANG) > 0);
    
    printf("[MAIN] ✅ Arrêt complet\n");
    
    return 0;
}
