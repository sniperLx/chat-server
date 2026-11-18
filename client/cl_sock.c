/*
 * cl_sock.c
 * Socket connection, login state, and receiver thread for the GTK client.
 *
 * The socket reader must not call GTK directly. It wraps each decoded packet in
 * an IncomingPacket and schedules packet_idle(), which runs on the GTK main
 * thread. Connection generations prevent a stale reader from changing the state
 * of a newer connection created after the old one was closed.
 */

#include "client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* A detached receiver receives an immutable snapshot of its socket identity. */
typedef struct {
	int fd;
	guint64 generation;
} ReceiveArg;

/* Ownership of payload transfers from the reader thread to packet_idle(). */
typedef struct {
	guint64 generation;
	uint8_t type;
	char *payload;
} IncomingPacket;

static pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;
static int sockfd = -1;
static int connected = -1;
static int logged_in;
static guint64 connection_generation;

/*
 * Apply one packet on the GTK main thread. Connection state is read under the
 * mutex, then released before touching widgets to avoid lock inversion with
 * close_connection() and the receiver thread.
 */
static gboolean packet_idle(gpointer data)
{
	IncomingPacket *packet = data;
	int current;
	int login_state = -1;

	pthread_mutex_lock(&connection_mutex);
	current = connected == 0 &&
		  connection_generation == packet->generation;
	if (current && packet->type == CHAT_MSG_LOGIN_OK) {
		logged_in = 1;
		login_state = 1;
	} else if (current && packet->type == CHAT_MSG_LOGIN_ERROR) {
		logged_in = 0;
		login_state = 0;
	}
	pthread_mutex_unlock(&connection_mutex);

	if (!current) {
		free(packet->payload);
		free(packet);
		return G_SOURCE_REMOVE;
	}

	if (login_state != -1)
		update_login_state_ui(login_state);

	switch (packet->type) {
	case CHAT_MSG_TEXT:
		show_remote_text(packet->payload);
		break;
	case CHAT_MSG_LOGIN_OK:
		show_err("Logged in as ");
		show_err(packet->payload);
		show_err(".\n");
		break;
	case CHAT_MSG_LOGIN_ERROR:
		show_err("Login failed: ");
		show_err(packet->payload);
		show_err("\n");
		request_login_name();
		break;
	default:
		show_err("Server sent an unsupported message.\n");
		close_connection();
		break;
	}

	free(packet->payload);
	free(packet);
	return G_SOURCE_REMOVE;
}

/* Notify the GUI only if the disconnect belongs to the visible connection. */
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

	update_login_state_ui(0);
	show_err("Connection closed.\n");
	return G_SOURCE_REMOVE;
}

/*
 * Tear down a receiver-owned connection exactly once. Returns whether this
 * generation was still current and therefore warrants a disconnect notice.
 */
static int finish_connection(int fd, guint64 generation)
{
	int current = 0;

	pthread_mutex_lock(&connection_mutex);
	if (sockfd == fd && connection_generation == generation) {
		sockfd = -1;
		connected = -1;
		logged_in = 0;
		current = 1;
	}
	pthread_mutex_unlock(&connection_mutex);

	shutdown(fd, SHUT_RDWR);
	close(fd);
	return current;
}

/*
 * Mark the active connection unusable while holding connection_mutex. Shutdown
 * wakes a blocked recv(); the receiver thread performs the final close.
 */
static void invalidate_connection(int fd)
{
	/* Caller holds connection_mutex. */
	logged_in = 0;
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

int connection_is_logged_in(void)
{
	int active;

	pthread_mutex_lock(&connection_mutex);
	active = connected == 0 && logged_in;
	pthread_mutex_unlock(&connection_mutex);
	return active;
}

/*
 * Invalidate the current socket from the GTK thread. The monotonic generation
 * change makes all pending reader/idle callbacks harmless.
 */
void close_connection(void)
{
	pthread_mutex_lock(&connection_mutex);
	if (sockfd != -1)
		invalidate_connection(sockfd);
	else {
		connected = -1;
		logged_in = 0;
		connection_generation++;
	}
	pthread_mutex_unlock(&connection_mutex);
}

/*
 * Detached reader: decode frames, hand them to the GTK thread, and clean up its
 * socket when EOF, a protocol error, or shutdown terminates the loop.
 */
void *recv_func(void *data)
{
	ReceiveArg *arg = data;
	int fd = arg->fd;
	guint64 generation = arg->generation;

	for (;;) {
		uint8_t type;
		char *payload = NULL;
		size_t length = 0;
		IncomingPacket *packet;
		int result = recv_packet(fd, &type, &payload, &length);

		if (result <= 0)
			break;
		(void)length;

		packet = malloc(sizeof(*packet));
		if (packet == NULL) {
			free(payload);
			break;
		}
		packet->generation = generation;
		packet->type = type;
		packet->payload = payload;
		if (g_idle_add(packet_idle, packet) == 0) {
			free(payload);
			free(packet);
		}
	}

	if (finish_connection(fd, generation))
		g_idle_add(disconnected_idle,
			   GSIZE_TO_POINTER((gsize)generation));

	free(arg);
	return NULL;
}

/*
 * Connect, publish the new socket/generation pair, and start its reader thread.
 * The second active-connection check closes the small race between the initial
 * check and a concurrent manual Connect request.
 */
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
	logged_in = 0;
	generation = connection_generation;
	pthread_mutex_unlock(&connection_mutex);
	update_login_state_ui(0);

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

/*
 * Send login credentials for the current connection. Setting logged_in = 0
 * prevents text sends until a LOGIN_OK packet is processed by the GTK thread.
 */
int send_login(const char *name)
{
	size_t length;
	int result = 0;

	if (name == NULL)
		return -1;
	length = strlen(name);
	if (length == 0 || length > CHAT_MAX_NAME)
		return -1;

	pthread_mutex_lock(&connection_mutex);
	if (connected != 0 || sockfd == -1) {
		result = -1;
	} else {
		logged_in = 0;
		if (send_packet(sockfd, CHAT_MSG_LOGIN, name, length) != 0) {
			invalidate_connection(sockfd);
			result = -1;
		}
	}
	pthread_mutex_unlock(&connection_mutex);

	return result;
}

/*
 * Serialize writes with connection teardown. Authentication is checked while
 * holding the mutex so close/logout cannot race with packet submission.
 */
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
	if (connected != 0 || sockfd == -1 || !logged_in) {
		result = -1;
	} else if (send_packet(sockfd, CHAT_MSG_TEXT, text, length) != 0) {
		invalidate_connection(sockfd);
		result = -1;
	}
	pthread_mutex_unlock(&connection_mutex);

	return result;
}
