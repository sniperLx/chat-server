/*
 * cl_sock.c
 * Socket connection and receiver thread for the GTK chat client.
 */

#include "client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
	int fd;
	guint64 generation;
} ReceiveArg;

static pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;
static int sockfd = -1;
static int connected = -1;
static guint64 connection_generation;

static gboolean remote_message_idle(gpointer data)
{
	char *message = data;

	show_remote_text(message);
	free(message);
	return G_SOURCE_REMOVE;
}

static gboolean disconnected_idle(gpointer data)
{
	guint64 generation = (guint64)GPOINTER_TO_SIZE(data);
	int show_notice;

	pthread_mutex_lock(&connection_mutex);
	show_notice = connected != 0 &&
		      connection_generation == generation;
	pthread_mutex_unlock(&connection_mutex);

	if (!show_notice)
		return G_SOURCE_REMOVE;

	show_err("Connection closed.\n");
	return G_SOURCE_REMOVE;
}

static int finish_connection(int fd, guint64 generation)
{
	int current = 0;

	pthread_mutex_lock(&connection_mutex);
	if (sockfd == fd && connection_generation == generation) {
		sockfd = -1;
		connected = -1;
		current = 1;
	}
	pthread_mutex_unlock(&connection_mutex);

	shutdown(fd, SHUT_RDWR);
	close(fd);
	return current;
}

static void invalidate_connection(int fd)
{
	/* Caller holds connection_mutex. */
	shutdown(fd, SHUT_RDWR);
	sockfd = -1;
	connected = -1;
	connection_generation++;
}

int connection_is_active(void)
{
	int active;

	pthread_mutex_lock(&connection_mutex);
	active = connected == 0;
	pthread_mutex_unlock(&connection_mutex);
	return active;
}

void close_connection(void)
{
	pthread_mutex_lock(&connection_mutex);
	if (sockfd != -1)
		invalidate_connection(sockfd);
	else {
		connected = -1;
		connection_generation++;
	}
	pthread_mutex_unlock(&connection_mutex);
}

void *recv_func(void *data)
{
	ReceiveArg *arg = data;
	int fd = arg->fd;
	guint64 generation = arg->generation;

	for (;;) {
		char *message = NULL;
		size_t length = 0;
		int result = recv_frame(fd, &message, &length);

		if (result <= 0)
			break;
		(void)length;

		if (g_idle_add(remote_message_idle, message) == 0)
			free(message);
	}

	if (finish_connection(fd, generation))
		g_idle_add(disconnected_idle,
			   GSIZE_TO_POINTER((gsize)generation));

	free(arg);
	return NULL;
}

int build_socket(const char *serv_ip)
{
	struct sockaddr_in address;
	struct timeval send_timeout;
	pthread_t receive_thread;
	pthread_attr_t thread_attr;
	ReceiveArg *arg;
	guint64 generation;
	int fd;
	int result;

	if (connection_is_active())
		return -2;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		return -1;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(CHAT_PORT);
	result = inet_pton(AF_INET, serv_ip, &address.sin_addr);
	if (result != 1) {
		close(fd);
		return result == 0 ? 1 : -1;
	}

	result = connect(fd, (struct sockaddr *)&address, sizeof(address));
	if (result == -1) {
		close(fd);
		return -1;
	}

	send_timeout.tv_sec = 5;
	send_timeout.tv_usec = 0;
	(void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
			 &send_timeout, sizeof(send_timeout));

	pthread_mutex_lock(&connection_mutex);
	if (connected == 0) {
		pthread_mutex_unlock(&connection_mutex);
		close(fd);
		return -2;
	}
	connection_generation++;
	sockfd = fd;
	connected = 0;
	generation = connection_generation;
	pthread_mutex_unlock(&connection_mutex);

	arg = malloc(sizeof(*arg));
	if (arg == NULL) {
		finish_connection(fd, generation);
		return -1;
	}
	arg->fd = fd;
	arg->generation = generation;

	result = pthread_attr_init(&thread_attr);
	if (result != 0) {
		free(arg);
		finish_connection(fd, generation);
		return -1;
	}
	result = pthread_attr_setdetachstate(&thread_attr,
					     PTHREAD_CREATE_DETACHED);
	if (result == 0)
		result = pthread_create(&receive_thread, &thread_attr,
					recv_func, arg);
	(void)pthread_attr_destroy(&thread_attr);

	if (result != 0) {
		free(arg);
		finish_connection(fd, generation);
		return -1;
	}

	return 0;
}

int send_func(const char *text)
{
	size_t length;
	int result = 0;

	if (text == NULL)
		return -1;
	length = strlen(text);
	if (length == 0 || length > CHAT_MAX_TEXT)
		return -1;

	pthread_mutex_lock(&connection_mutex);
	if (connected != 0 || sockfd == -1) {
		result = -1;
	} else if (send_frame(sockfd, text, length) != 0) {
		invalidate_connection(sockfd);
		result = -1;
	}
	pthread_mutex_unlock(&connection_mutex);

	return result;
}
