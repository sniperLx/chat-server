#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <stddef.h>

#define CHAT_PORT 8000
#define CHAT_MAX_FRAME 4096
#define CHAT_MAX_TEXT 4000

/*
 * Each frame is a network-byte-order uint32_t length followed by exactly
 * that many payload bytes. The terminating NUL is not part of the frame.
 */
int send_frame(int fd, const char *message, size_t length);
int recv_frame(int fd, char **message, size_t *length);

#endif
