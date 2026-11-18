#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Socket writes may be partial. Keep sending the remaining bytes until the
 * frame is complete; MSG_NOSIGNAL turns a peer disconnect into EPIPE.
 */
static int send_all(int fd, const void *buffer, size_t length)
{
	const char *data = buffer;
	size_t sent = 0;

	while (sent < length) {
		ssize_t result = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (result == 0)
			return -1;
		sent += (size_t)result;
	}

	return 0;
}

/*
 * Return 1 after a complete read, 0 for a clean EOF before the first byte, and
 * -1 for an error or EOF after a partial read. We cannot safely hand a partial
 * frame to the protocol parser.
 */
static int recv_all(int fd, void *buffer, size_t length)
{
	char *data = buffer;
	size_t received = 0;

	while (received < length) {
		ssize_t result = recv(fd, data + received, length - received, 0);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (result == 0)
			return received == 0 ? 0 : -1;
		received += (size_t)result;
	}

	return 1;
}

int send_packet(int fd, uint8_t type, const char *payload, size_t length)
{
	uint32_t network_length;

	/* The frame stores "type + payload", so the payload must leave room for it. */
	if (length > CHAT_MAX_FRAME - 1)
		return -1;
	/* NUL would truncate payloads when they are later treated as C strings. */
	if (length > 0 &&
	    (payload == NULL || memchr(payload, '\0', length) != NULL))
		return -1;

	/* Length is sent once, in network byte order, before the type and body. */
	network_length = htonl((uint32_t)length + 1);
	if (send_all(fd, &network_length, sizeof(network_length)) != 0)
		return -1;
	if (send_all(fd, &type, sizeof(type)) != 0)
		return -1;
	if (length == 0)
		return 0;

	return send_all(fd, payload, length);
}

int recv_packet(int fd, uint8_t *type, char **payload, size_t *length)
{
	uint32_t network_length;
	uint32_t payload_length;
	char *packet;
	int result;

	if (type == NULL || payload == NULL || length == NULL)
		return -1;

	*type = 0;
	*payload = NULL;
	*length = 0;

	/* EOF before a length prefix is a clean disconnect; a partial one is not. */
	result = recv_all(fd, &network_length, sizeof(network_length));
	if (result <= 0)
		return result;

	payload_length = ntohl(network_length);
	/* Reject zero-size frames and lengths that could trigger huge allocations. */
	if (payload_length < 1 || payload_length > CHAT_MAX_FRAME)
		return -1;

	/* The extra byte lets callers use ordinary C string functions on payload. */
	packet = malloc((size_t)payload_length + 1);
	if (packet == NULL)
		return -1;

	result = recv_all(fd, packet, payload_length);
	if (result <= 0) {
		free(packet);
		return result;
	}

	if (memchr(packet, '\0', payload_length) != NULL) {
		free(packet);
		return -1;
	}

	/*
	 * Remove the type byte in place so the returned allocation starts at the
	 * payload. Ownership transfers to the caller only after this point.
	 */
	*type = (uint8_t)packet[0];
	payload_length--;
	memmove(packet, packet + 1, payload_length);
	packet[payload_length] = '\0';
	*payload = packet;
	*length = payload_length;
	return 1;
}
