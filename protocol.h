#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define CHAT_PORT 8000
#define CHAT_MAX_FRAME 4096
#define CHAT_MAX_TEXT 4000
#define CHAT_MAX_NAME 32

enum chat_message_type {
	CHAT_MSG_LOGIN = 1,
	CHAT_MSG_TEXT = 2,
	CHAT_MSG_LOGIN_OK = 3,
	CHAT_MSG_LOGIN_ERROR = 4
};

/*
 * Each frame is a network-byte-order uint32_t length followed by a one-byte
 * message type and length - 1 payload bytes. The terminating NUL is not part
 * of the frame.
 */
int send_packet(int fd, uint8_t type, const char *payload, size_t length);
int recv_packet(int fd, uint8_t *type, char **payload, size_t *length);

#endif
