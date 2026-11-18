#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

int send_frame(int fd, const char *message, size_t length)
{
	uint32_t network_length;

	if (message == NULL || length == 0 || length > CHAT_MAX_FRAME)
		return -1;
	if (memchr(message, '\0', length) != NULL)
		return -1;

	network_length = htonl((uint32_t)length);
	if (send_all(fd, &network_length, sizeof(network_length)) != 0)
		return -1;

	return send_all(fd, message, length);
}

int recv_frame(int fd, char **message, size_t *length)
{
	uint32_t network_length;
	uint32_t payload_length;
	char *payload;
	int result;

	if (message == NULL || length == NULL)
		return -1;

	*message = NULL;
	*length = 0;
	result = recv_all(fd, &network_length, sizeof(network_length));
	if (result <= 0)
		return result;

	payload_length = ntohl(network_length);
	if (payload_length == 0 || payload_length > CHAT_MAX_FRAME)
		return -1;

	payload = malloc((size_t)payload_length + 1);
	if (payload == NULL)
		return -1;

	result = recv_all(fd, payload, payload_length);
	if (result <= 0) {
		free(payload);
		return result;
	}

	payload[payload_length] = '\0';
	*message = payload;
	*length = payload_length;
	return 1;
}
