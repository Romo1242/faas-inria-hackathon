#pragma once

#include <stddef.h>
#include <sys/types.h>

#define LB_SOCK_PATH "/tmp/faas_lb.sock"
#define SERVER_SOCK_PATH "/tmp/faas_server.sock"

int create_unix_server_socket(const char *path);     // returns listen fd
int create_unix_client_socket(const char *path);     // returns connected fd
int set_nonblocking(int fd);

ssize_t write_all(int fd, const void *buf, size_t len);
ssize_t read_line(int fd, char *buf, size_t maxlen); // reads up to \n (included)

void trim_newline(char *s);

void die(const char *msg);
void warnx(const char *msg);
