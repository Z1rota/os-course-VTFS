#ifndef VTFS_HTTP_H
#define VTFS_HTTP_H

#include <linux/inet.h>

#define VTFS_INODE_INFO_SIZE 25

int64_t vtfs_http_call(const char *token, const char *method,
                            char *response_buffer, size_t buffer_size,
                            size_t *response_length, size_t arg_size, ...);

void encode(const char *, char *);

int vtfs_http_post(const char *path, const void *body, size_t body_size,
                   char *response, size_t response_size, size_t *received);
#endif 
