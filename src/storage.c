#include "storage.h"
#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

void generate_function_id(char *buf, size_t len, const char *name) {
    // Simple ID: name + timestamp
    time_t now = time(NULL);
    snprintf(buf, len, "%s_%ld", name, (long)now);
}

static const char* get_file_extension(const char *lang) {
    if (strcmp(lang, "c") == 0) return "c";
    if (strcmp(lang, "js") == 0 || strcmp(lang, "javascript") == 0) return "js";
    if (strcmp(lang, "python") == 0 || strcmp(lang, "py") == 0) return "py";
    if (strcmp(lang, "rust") == 0 || strcmp(lang, "rs") == 0) return "rs";
    if (strcmp(lang, "go") == 0) return "go";
    if (strcmp(lang, "php") == 0) return "php";
    if (strcmp(lang, "html") == 0) return "html";
    return "txt";
}

int store_function(const char *name, const char *lang, const char *code, size_t code_len, char *out_id) {
    char id[MAX_FUNC_ID];
    generate_function_id(id, sizeof(id), name);

    // Create function directory
    char func_dir[512];
    snprintf(func_dir, sizeof(func_dir), "%s/%s", FUNCTIONS_DIR, id);
    if (mkdir(FUNCTIONS_DIR, 0755) < 0 && errno != EEXIST) {
        perror("mkdir functions");
        return -1;
    }
    if (mkdir(func_dir, 0755) < 0) {
        perror("mkdir function dir");
        return -1;
    }

    // Write code file
    char code_path[512];
    const char *ext = get_file_extension(lang);
    snprintf(code_path, sizeof(code_path), "%s/code.%s", func_dir, ext);
    int fd = open(code_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open code file");
        return -1;
    }
    if (write(fd, code, code_len) != (ssize_t)code_len) {
        perror("write code");
        close(fd);
        return -1;
    }
    close(fd);

    // Write metadata
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", func_dir);
    FILE *mf = fopen(meta_path, "w");
    if (!mf) {
        perror("fopen metadata");
        return -1;
    }
    fprintf(mf, "{\n");
    fprintf(mf, "  \"id\": \"%s\",\n", id);
    fprintf(mf, "  \"name\": \"%s\",\n", name);
    fprintf(mf, "  \"language\": \"%s\",\n", lang);
    fprintf(mf, "  \"entrypoint\": \"main\",\n");
    fprintf(mf, "  \"created_at\": \"%ld\",\n", (long)time(NULL));
    fprintf(mf, "  \"size\": %zu\n", code_len);
    fprintf(mf, "}\n");
    fclose(mf);

    strncpy(out_id, id, MAX_FUNC_ID - 1);
    return 0;
}

int load_function(const char *id, char *code_buf, size_t buf_len) {
    // Try to find code file (try multiple extensions)
    const char *exts[] = {"c", "js", "py", "rs", "go", "wasm", NULL};
    for (int i = 0; exts[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s/code.%s", FUNCTIONS_DIR, id, exts[i]);
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(code_buf, 1, buf_len - 1, f);
            code_buf[n] = '\0';
            fclose(f);
            return (int)n;
        }
    }
    return -1;
}

int load_function_metadata(const char *id, function_metadata_t *meta) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/metadata.json", FUNCTIONS_DIR, id);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    // Simple JSON parsing (naive)
    char line[512];
    memset(meta, 0, sizeof(*meta));
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "\"id\"")) {
            sscanf(line, " \"id\": \"%127[^\"]\"", meta->id);
        } else if (strstr(line, "\"name\"")) {
            sscanf(line, " \"name\": \"%63[^\"]\"", meta->name);
        } else if (strstr(line, "\"language\"")) {
            sscanf(line, " \"language\": \"%15[^\"]\"", meta->language);
        } else if (strstr(line, "\"entrypoint\"")) {
            sscanf(line, " \"entrypoint\": \"%63[^\"]\"", meta->entrypoint);
        } else if (strstr(line, "\"size\"")) {
            sscanf(line, " \"size\": %zu", &meta->size);
        }
    }
    fclose(f);
    return 0;
}

int function_exists(const char *id) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/metadata.json", FUNCTIONS_DIR, id);
    return access(path, F_OK) == 0;
}

int function_name_exists(const char *name) {
    char func_id[MAX_FUNC_ID];
    return find_function_by_name(name, func_id) == 0;
}

int find_function_by_name(const char *name, char *out_id) {
    // Scan functions directory for matching name
    DIR *dir = opendir(FUNCTIONS_DIR);
    if (!dir) return -1;
    
    struct dirent *entry;
    time_t latest_time = 0;
    int found = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        // Load metadata for this function
        function_metadata_t meta;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s/metadata.json", FUNCTIONS_DIR, entry->d_name);
        
        FILE *f = fopen(path, "r");
        if (!f) continue;
        
        char line[512];
        char func_name[MAX_FUNC_NAME] = {0};
        time_t created_at = 0;
        
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "\"name\"")) {
                sscanf(line, " \"name\": \"%63[^\"]\"", func_name);
            } else if (strstr(line, "\"created_at\"")) {
                long ts;
                sscanf(line, " \"created_at\": \"%ld\"", &ts);
                created_at = (time_t)ts;
            }
        }
        fclose(f);
        
        // Check if name matches
        if (strcmp(func_name, name) == 0) {
            if (!found || created_at > latest_time) {
                strncpy(out_id, entry->d_name, MAX_FUNC_ID - 1);
                latest_time = created_at;
                found = 1;
            }
        }
    }
    
    closedir(dir);
    return found ? 0 : -1;
}
