#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Shared wire limits. A frame includes a type byte plus at most 4095 data bytes. */
#define CHAT_PORT 8000
#define CHAT_MAX_FRAME 4096
/* Leaves room for the server's "name: " prefix inside CHAT_MAX_FRAME. */
#define CHAT_MAX_TEXT 4000
#define CHAT_MAX_NAME 32

/* Message types used by both the client and the server state machines. */
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

/*
 * Send one complete frame. Returns 0 on success and -1 on invalid arguments,
 * a closed socket, or any I/O error. The function retries interrupted sends.
 */
int send_packet(int fd, uint8_t type, const char *payload, size_t length);

/*
 * Receive one complete frame and allocate a NUL-terminated copy of its payload.
 * Returns 1 with output parameters populated, 0 only when EOF occurs before any
 * bytes of the next frame, and -1 for malformed input, allocation/I/O errors,
 * or EOF in the middle of a frame. The caller owns *payload on success.
 */
int recv_packet(int fd, uint8_t *type, char **payload, size_t *length);

#endif
