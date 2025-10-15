#pragma once

#include <stddef.h>

#define FUNCTIONS_DIR "functions"
#define MAX_FUNC_NAME 64
#define MAX_FUNC_ID 128

typedef struct {
    char id[MAX_FUNC_ID];
    char name[MAX_FUNC_NAME];
    char language[16];
    char entrypoint[64];
    size_t size;
} function_metadata_t;

// Generate unique function ID
void generate_function_id(char *buf, size_t len, const char *name);

// Store function code and metadata
int store_function(const char *name, const char *lang, const char *code, size_t code_len, char *out_id);

// Load function code by ID
int load_function(const char *id, char *code_buf, size_t buf_len);

// Load function metadata by ID
int load_function_metadata(const char *id, function_metadata_t *meta);

// Check if function exists by ID
int function_exists(const char *id);

// Check if function name already exists
int function_name_exists(const char *name);

// Find function ID by name (returns most recent if multiple)
int find_function_by_name(const char *name, char *out_id);
