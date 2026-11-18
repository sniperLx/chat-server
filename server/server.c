/*
 * server.c
 * Multi-client TCP chat server. Messages are relayed to every other client.
 */

#include "server.h"

typedef struct {
	int fd;
	int slot;
} ClientArg;

static int client_fds[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t broadcast_mutex = PTHREAD_MUTEX_INITIALIZER;

static int reserve_client_slot(int fd)
{
	int slot;

	pthread_mutex_lock(&clients_mutex);
	for (slot = 0; slot < MAX_CLIENTS; slot++) {
		if (client_fds[slot] == -1) {
			client_fds[slot] = fd;
			pthread_mutex_unlock(&clients_mutex);
			return slot;
		}
	}
	pthread_mutex_unlock(&clients_mutex);
	return -1;
}

static void release_client_slot(int slot, int fd)
{
	int release = 0;

	pthread_mutex_lock(&clients_mutex);
	if (slot >= 0 && slot < MAX_CLIENTS && client_fds[slot] == fd) {
		client_fds[slot] = -1;
		release = 1;
	}
	pthread_mutex_unlock(&clients_mutex);

	if (release) {
		shutdown(fd, SHUT_RDWR);
		close(fd);
	}
}

static void broadcast_message(int sender_fd, const char *message,
			      size_t length)
{
	int duplicates[MAX_CLIENTS];
	int duplicate_count = 0;
	int slot;

	pthread_mutex_lock(&clients_mutex);
	for (slot = 0; slot < MAX_CLIENTS; slot++) {
		int fd = client_fds[slot];

		if (fd == -1 || fd == sender_fd)
			continue;

		/*
		 * Keep the socket alive after releasing the mutex. This avoids
		 * sending through a descriptor that another thread just closed
		 * and whose number may have been reused.
		 */
		fd = dup(fd);
		if (fd != -1)
			duplicates[duplicate_count++] = fd;
	}
	pthread_mutex_unlock(&clients_mutex);

	pthread_mutex_lock(&broadcast_mutex);
	for (slot = 0; slot < duplicate_count; slot++) {
		if (send_frame(duplicates[slot], message, length) != 0)
			shutdown(duplicates[slot], SHUT_RDWR);
		close(duplicates[slot]);
	}
	pthread_mutex_unlock(&broadcast_mutex);
}

void *client_thread(void *data)
{
	ClientArg *arg = data;
	int fd = arg->fd;
	int slot = arg->slot;

	for (;;) {
		char *message = NULL;
		char outbound[CHAT_MAX_FRAME + 1];
		size_t length = 0;
		int result;
		int written;

		result = recv_frame(fd, &message, &length);
		if (result <= 0)
			break;

		if (length > CHAT_MAX_TEXT ||
		    memchr(message, '\0', length) != NULL) {
			free(message);
			fprintf(stderr, "client(fd=%d) sent an invalid message\n",
				fd);
			break;
		}

		written = snprintf(outbound, sizeof(outbound), "fd_%d:%.*s",
				   fd, (int)length, message);
		if (written <= 0 || (size_t)written >= sizeof(outbound)) {
			free(message);
			continue;
		}

		printf("client(fd=%d): %s\n", fd, message);
		broadcast_message(fd, outbound, (size_t)written);
		free(message);
	}

	printf("client(fd=%d) disconnected\n", fd);
	release_client_slot(slot, fd);
	free(arg);
	return NULL;
}

int run_server(void)
{
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	pthread_attr_t thread_attr;
	socklen_t client_address_length;
	int server_fd;
	int reuse = 1;
	int slot;
	int result;

	for (slot = 0; slot < MAX_CLIENTS; slot++)
		client_fds[slot] = -1;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		perror("socket");
		return -1;
	}
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
		       &reuse, sizeof(reuse)) == -1) {
		perror("setsockopt");
		close(server_fd);
		return -1;
	}

	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(CHAT_PORT);

	if (bind(server_fd, (struct sockaddr *)&server_address,
		 sizeof(server_address)) == -1) {
		perror("bind");
		close(server_fd);
		return -1;
	}
	if (listen(server_fd, MAX_CLIENTS) == -1) {
		perror("listen");
		close(server_fd);
		return -1;
	}

	result = pthread_attr_init(&thread_attr);
	if (result != 0) {
		fprintf(stderr, "pthread_attr_init: %s\n", strerror(result));
		close(server_fd);
		return -1;
	}
	result = pthread_attr_setdetachstate(&thread_attr,
					     PTHREAD_CREATE_DETACHED);
	if (result != 0) {
		fprintf(stderr, "pthread_attr_setdetachstate: %s\n",
			strerror(result));
		pthread_attr_destroy(&thread_attr);
		close(server_fd);
		return -1;
	}

	printf("chat server listening on port %d\n", CHAT_PORT);
	for (;;) {
		ClientArg *arg;
		struct timeval send_timeout;
		pthread_t thread;
		int client_fd;

		client_address_length = sizeof(client_address);
		client_fd = accept(server_fd,
				   (struct sockaddr *)&client_address,
				   &client_address_length);
		if (client_fd == -1) {
			if (errno == EINTR)
				continue;
			perror("accept");
			continue;
		}

		send_timeout.tv_sec = 5;
		send_timeout.tv_usec = 0;
		(void)setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
				 &send_timeout, sizeof(send_timeout));

		slot = reserve_client_slot(client_fd);
		if (slot == -1) {
			printf("connection rejected: server is full\n");
			close(client_fd);
			continue;
		}

		arg = malloc(sizeof(*arg));
		if (arg == NULL) {
			release_client_slot(slot, client_fd);
			continue;
		}
		arg->fd = client_fd;
		arg->slot = slot;

		result = pthread_create(&thread, &thread_attr,
					client_thread, arg);
		if (result != 0) {
			fprintf(stderr, "pthread_create: %s\n",
				strerror(result));
			free(arg);
			release_client_slot(slot, client_fd);
			continue;
		}

		printf("client connected from %s:%d (fd=%d)\n",
		       inet_ntoa(client_address.sin_addr),
		       ntohs(client_address.sin_port), client_fd);
	}
}

int main(void)
{
	return run_server() == 0 ? 0 : 1;
}
