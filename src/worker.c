#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "ipc.h"
#include "storage.h"

// Enable Wasmer (requires libwasmer)
#define USE_WASMER
#ifdef USE_WASMER
#include <wasm.h>
#include <wasmer.h>
#endif

#define LINE_MAX 8192
#define CODE_BUF_SIZE 65536

#ifdef USE_WASMER
// Execute WASM function using Wasmer C API 3.x with WASI support
// NO FORK! Worker is already a forked process from Server
static int execute_wasm(const char *wasm_path, const char *func_name, char *output, size_t out_len) {
    fprintf(stderr, "[WORKER] 🚀 Execute WASM directly in worker process (PID %d)\n", getpid());
    
    // Redirect stdout to capture output
    int stdout_backup = dup(STDOUT_FILENO);
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        fprintf(stderr, "[WORKER] ❌ Pipe creation failed\n");
        snprintf(output, out_len, "pipe creation failed");
        return -1;
    }
    
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    
    // Load WASM file
    fprintf(stderr, "[WORKER] 📂 Loading WASM file: %s\n", wasm_path);
    FILE *file = fopen(wasm_path, "rb");
    if (!file) {
        fprintf(stderr, "[WORKER] ❌ Cannot open wasm file: %s\n", wasm_path);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "cannot open wasm file");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);
    uint8_t *wasm_bytes = malloc(file_size);
    if (!wasm_bytes) {
        fclose(file);
        fprintf(stderr, "[WORKER] ❌ Malloc failed\n");
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "malloc failed");
        return -1;
    }
    fread(wasm_bytes, 1, file_size, file);
    fclose(file);

    fprintf(stderr, "[WORKER] 📦 Loaded WASM file: %zu bytes\n", file_size);

    // Initialize Wasmer 3.x
    fprintf(stderr, "[WORKER] 🔧 Initializing Wasmer engine...\n");
    wasm_engine_t *engine = wasm_engine_new();
    wasm_store_t *store = wasm_store_new(engine);

    wasm_byte_vec_t binary;
    wasm_byte_vec_new_uninitialized(&binary, file_size);
    memcpy(binary.data, wasm_bytes, file_size);
    free(wasm_bytes);

    wasm_module_t *module = wasm_module_new(store, &binary);
    wasm_byte_vec_delete(&binary);
    
    if (!module) {
        fprintf(stderr, "[WORKER] ❌ Failed to compile wasm module\n");
        wasm_store_delete(store);
        wasm_engine_delete(engine);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "failed to compile wasm module");
        return -1;
    }

    fprintf(stderr, "[WORKER] ✅ WASM module compiled\n");

    // WASI setup
    wasi_config_t *wasi_config = wasi_config_new("worker");
    if (!wasi_config) {
        fprintf(stderr, "[WORKER] ❌ Failed to create WASI config\n");
        wasm_module_delete(module);
        wasm_store_delete(store);
        wasm_engine_delete(engine);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "failed to create WASI config");
        return -1;
    }
    
    wasi_config_inherit_stdout(wasi_config);
    wasi_config_inherit_stderr(wasi_config);
    wasi_config_inherit_stdin(wasi_config);
    
    wasi_env_t *wasi_env = wasi_env_new(store, wasi_config);
    if (!wasi_env) {
        fprintf(stderr, "[WORKER] ❌ Failed to create WASI environment\n");
        wasm_module_delete(module);
        wasm_store_delete(store);
        wasm_engine_delete(engine);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "failed to create WASI environment");
        return -1;
    }

    fprintf(stderr, "[WORKER] ✅ WASI environment created\n");

    // Get WASI imports
    wasm_extern_vec_t imports;
    bool ok = wasi_get_imports(store, wasi_env, module, &imports);
    if (!ok) {
        fprintf(stderr, "[WORKER] ❌ Failed to get WASI imports\n");
        wasm_module_delete(module);
        wasm_store_delete(store);
        wasm_engine_delete(engine);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "failed to get WASI imports");
        return -1;
    }

    fprintf(stderr, "[WORKER] ✅ WASI imports retrieved\n");

    // Instantiate module
    wasm_instance_t *instance = wasm_instance_new(store, module, &imports, NULL);
    if (!instance) {
        fprintf(stderr, "[WORKER] ❌ Failed to instantiate wasm module\n");
        wasm_extern_vec_delete(&imports);
        wasm_module_delete(module);
        wasm_store_delete(store);
        wasm_engine_delete(engine);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "failed to instantiate wasm module");
        return -1;
    }

    fprintf(stderr, "[WORKER] ✅ WASM instance created\n");

    // Get exports
    wasm_extern_vec_t exports;
    wasm_instance_exports(instance, &exports);

    wasm_exporttype_vec_t export_types;
    wasm_module_exports(module, &export_types);

    // Find target function
    wasm_func_t *target_func = NULL;
    for (size_t i = 0; i < export_types.size && i < exports.size; ++i) {
        const wasm_exporttype_t *et = export_types.data[i];
        const wasm_name_t *name = wasm_exporttype_name(et);
        if (name && name->size == strlen(func_name) && 
            memcmp(name->data, func_name, name->size) == 0) {
            if (wasm_extern_kind(exports.data[i]) == WASM_EXTERN_FUNC) {
                target_func = wasm_extern_as_func(exports.data[i]);
                fprintf(stderr, "[WORKER] ✅ Found function: %s\n", func_name);
                break;
            }
        }
    }

    if (!target_func) {
        fprintf(stderr, "[WORKER] ❌ Function '%s' not found in exports\n", func_name);
        wasm_extern_vec_delete(&exports);
        wasm_exporttype_vec_delete(&export_types);
        wasm_instance_delete(instance);
        wasm_extern_vec_delete(&imports);
        wasm_module_delete(module);
        wasm_store_delete(store);
        wasm_engine_delete(engine);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
        close(pipefd[0]);
        snprintf(output, out_len, "function '%s' not found", func_name);
        return -1;
    }

    // Prepare arguments and results
    size_t param_arity = wasm_func_param_arity(target_func);
    size_t result_arity = wasm_func_result_arity(target_func);

    fprintf(stderr, "[WORKER] 🔧 Function signature: %zu params, %zu results\n", param_arity, result_arity);

    wasm_val_vec_t args_vec;
    wasm_val_vec_new_uninitialized(&args_vec, param_arity);
    for (size_t i = 0; i < param_arity; ++i) {
        args_vec.data[i].kind = WASM_I32;
        args_vec.data[i].of.i32 = 0;
    }

    wasm_val_vec_t results_vec;
    wasm_val_vec_new_uninitialized(&results_vec, result_arity);

    // Call function (stdout is redirected to pipe)
    fprintf(stderr, "[WORKER] ⚡ Executing WASM function...\n");
    wasm_trap_t *trap = wasm_func_call(target_func, &args_vec, &results_vec);
    if (trap) {
        wasm_message_t message;
        wasm_trap_message(trap, &message);
        fprintf(stderr, "[WORKER] ❌ WASM trap: %.*s\n", (int)message.size, message.data);
        wasm_trap_delete(trap);
    } else {
        fprintf(stderr, "[WORKER] ✅ Execution completed successfully\n");
    }

    // Restore stdout
    fflush(stdout);
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdout_backup);

    // Read captured output from pipe
    size_t total_read = 0;
    ssize_t n;
    while (total_read < out_len - 1 && 
           (n = read(pipefd[0], output + total_read, out_len - total_read - 1)) > 0) {
        total_read += n;
    }
    output[total_read] = '\0';
    close(pipefd[0]);

    fprintf(stderr, "[WORKER] 📤 Captured output (%zu bytes): %s\n", total_read, output);

    // Cleanup Wasmer
    wasm_val_vec_delete(&args_vec);
    wasm_val_vec_delete(&results_vec);
    wasm_extern_vec_delete(&exports);
    wasm_exporttype_vec_delete(&export_types);
    wasm_instance_delete(instance);
    wasm_extern_vec_delete(&imports);
    wasm_module_delete(module);
    wasm_store_delete(store);
    wasm_engine_delete(engine);

    fprintf(stderr, "[WORKER] ✅ Wasmer cleanup complete\n");
    return 0;
}
#endif

// Check if WASM file exists (should be compiled by API Gateway during deploy)
static int check_wasm_exists(const char *func_id, char *wasm_path, size_t wasm_path_len) {
    snprintf(wasm_path, wasm_path_len, "%s/%s/code.wasm", FUNCTIONS_DIR, func_id);
    return access(wasm_path, F_OK) == 0 ? 0 : -1;
}

// Execute function based on language
static int execute_function(const char *func_id, const char *payload, char *output, size_t out_len) {
    fprintf(stderr, "[WORKER] 🔍 Loading metadata for: %s\n", func_id);
    
    function_metadata_t meta;
    if (load_function_metadata(func_id, &meta) < 0) {
        fprintf(stderr, "[WORKER] ❌ Metadata not found\n");
        snprintf(output, out_len, "function not found");
        return -1;
    }

    fprintf(stderr, "[WORKER] ✅ Metadata loaded: lang=%s\n", meta.language);

    char code_path[512];
    const char *ext = (strcmp(meta.language, "c") == 0) ? "c" :
                      (strcmp(meta.language, "js") == 0 || strcmp(meta.language, "javascript") == 0) ? "js" :
                      (strcmp(meta.language, "python") == 0 || strcmp(meta.language, "py") == 0) ? "py" :
                      (strcmp(meta.language, "rust") == 0 || strcmp(meta.language, "rs") == 0) ? "rs" :
                      (strcmp(meta.language, "go") == 0) ? "go" :
                      (strcmp(meta.language, "php") == 0) ? "php" :
                      (strcmp(meta.language, "html") == 0) ? "html" : "txt";
    snprintf(code_path, sizeof(code_path), "%s/%s/code.%s", FUNCTIONS_DIR, func_id, ext);

    // Strategy 1: Execute WASM with Wasmer (C/Rust/Go)
    if (strcmp(meta.language, "c") == 0 || strcmp(meta.language, "rust") == 0 || 
        strcmp(meta.language, "rs") == 0 || strcmp(meta.language, "go") == 0) {
        fprintf(stderr, "[WORKER] 🎯 Detected WASM language: %s\n", meta.language);
        char wasm_path[512];
        if (check_wasm_exists(func_id, wasm_path, sizeof(wasm_path)) == 0) {
            fprintf(stderr, "[WORKER] ✅ WASM file found: %s\n", wasm_path);
#ifdef USE_WASMER
            fprintf(stderr, "[WORKER] 🚀 Executing WASM with Wasmer...\n");
            return execute_wasm(wasm_path, "_start", output, out_len);
#else
            fprintf(stderr, "[WORKER] ❌ USE_WASMER not defined!\n");
            snprintf(output, out_len, "wasm file found at %s (Wasmer not enabled, rebuild with -DUSE_WASMER and link libwasmer)", wasm_path);
            return 0;
#endif
        } else {
            fprintf(stderr, "[WORKER] ❌ WASM file not found: %s\n", wasm_path);
            snprintf(output, out_len, "wasm file not found (should be compiled during deploy)");
            return -1;
        }
    }
    
    // Strategy 2: Execute with native runtime (JS/Python)
    else if (strcmp(meta.language, "js") == 0 || strcmp(meta.language, "javascript") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "node %s 2>&1", code_path);
        FILE *fp = popen(cmd, "r");
        if (!fp) {
            snprintf(output, out_len, "failed to execute js (install node)");
            return -1;
        }
        size_t n = fread(output, 1, out_len - 1, fp);
        output[n] = '\0';
        pclose(fp);
        return 0;
    }
    else if (strcmp(meta.language, "python") == 0 || strcmp(meta.language, "py") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "python3 %s 2>&1", code_path);
        FILE *fp = popen(cmd, "r");
        if (!fp) {
            snprintf(output, out_len, "failed to execute python (install python3)");
            return -1;
        }
        size_t n = fread(output, 1, out_len - 1, fp);
        output[n] = '\0';
        pclose(fp);
        return 0;
    }
    else if (strcmp(meta.language, "php") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "php %s 2>&1", code_path);
        FILE *fp = popen(cmd, "r");
        if (!fp) {
            snprintf(output, out_len, "failed to execute php (install php-cli)");
            return -1;
        }
        size_t n = fread(output, 1, out_len - 1, fp);
        output[n] = '\0';
        pclose(fp);
        return 0;
    }
    else if (strcmp(meta.language, "html") == 0) {
        // HTML: just read and return the file content
        FILE *fp = fopen(code_path, "r");
        if (!fp) {
            snprintf(output, out_len, "failed to read html file");
            return -1;
        }
        size_t n = fread(output, 1, out_len - 1, fp);
        output[n] = '\0';
        fclose(fp);
        return 0;
    }
    
    // Fallback: echo metadata
    snprintf(output, out_len, "executed %s (lang=%s) with payload=%s", 
             meta.name, meta.language, payload);
    return 0;
}

static void run_worker_loop(int fd) {
    char line[LINE_MAX];
    for (;;) {
        ssize_t n = read_line(fd, line, sizeof(line));
        if (n <= 0) {
            fprintf(stderr, "worker: connection closed\n");
            break;
        }
        // Accept both "type":"job" and "type":"invoke"
        if (strstr(line, "\"type\":\"job\"") || strstr(line, "\"type\":\"invoke\"")) {
            fprintf(stderr, "[WORKER] 📨 Received message: %s\n", line);
            
            // Extract fn and payload
            const char *fnp = strstr(line, "\"fn\":");
            const char *plp = strstr(line, "\"payload\":");
            if (!fnp || !plp) {
                fprintf(stderr, "[WORKER] ❌ Missing fn or payload in message\n");
                const char *resp = "{\"ok\":false,\"error\":\"no fn or payload\"}\n";
                write_all(fd, resp, strlen(resp));
                continue;
            }

            char func_id[MAX_FUNC_ID] = {0};
            char payload[LINE_MAX] = {0};
            sscanf(fnp, "\"fn\":\"%127[^\"]\"", func_id);
            sscanf(plp, "\"payload\":\"%8191[^\"]\"", payload);

            fprintf(stderr, "[WORKER] 📨 Received job: fn=%s, payload=%s\n", func_id, payload);

            // Execute function
            char output[LINE_MAX];
            fprintf(stderr, "[WORKER] 🚀 Calling execute_function()...\n");
            if (execute_function(func_id, payload, output, sizeof(output)) < 0) {
                fprintf(stderr, "[WORKER] ❌ Execution failed: %s\n", output);
                char resp[LINE_MAX];
                snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}\n", output);
                write_all(fd, resp, strlen(resp));
            } else {
                fprintf(stderr, "[WORKER] ✅ Execution succeeded: %s\n", output);
                char resp[LINE_MAX];
                snprintf(resp, sizeof(resp), "{\"ok\":true,\"output\":\"%s\"}\n", output);
                write_all(fd, resp, strlen(resp));
            }
        } else {
            const char *resp = "{\"ok\":false,\"error\":\"unknown job\"}\n";
            write_all(fd, resp, strlen(resp));
        }
    }
}

int main(void) {
    // Worker now reads jobs from stdin (pipe from server)
    const char *worker_id = getenv("WORKER_ID");
    if (!worker_id) worker_id = "unknown";
    
    fprintf(stderr, "worker[%s] pid=%d started, reading from stdin\n", worker_id, getpid());
    
    // Read jobs from stdin (pipe from server)
    run_worker_loop(STDIN_FILENO);
    
    fprintf(stderr, "worker[%s] exiting\n", worker_id);
    return 0;
}
