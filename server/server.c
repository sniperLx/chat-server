/*
 * server.c
 * Single-threaded, poll-driven multi-client chat server.
 */

#include "server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * A session owns all state needed between poll iterations. No Client field is
 * shared with another thread, so the server needs no locks.
 *
 * input holds bytes received from the peer, which may contain a fragment of one
 * frame or several complete frames. output holds encoded bytes not yet accepted
 * by the kernel; output_start..output_end is the live FIFO range.
 */
typedef struct {
	int fd;
	char name[CHAT_MAX_NAME + 1];
	int authenticated;
	unsigned char input[CLIENT_INPUT_SIZE];
	size_t input_length;
	unsigned char output[CLIENT_OUTPUT_SIZE];
	size_t output_start;
	size_t output_end;
} Client;

static Client clients[MAX_CLIENTS];

static void broadcast_packet(Client *sender, uint8_t type,
			     const char *payload, size_t length);

/* poll() requires nonblocking descriptors so one client cannot stall the loop. */
static int set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags == -1)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Mark a slot unused without performing any socket I/O. */
static void reset_client(Client *client)
{
	client->fd = -1;
	client->name[0] = '\0';
	client->authenticated = 0;
	client->input_length = 0;
	client->output_start = 0;
	client->output_end = 0;
}

/*
 * Encode one complete frame into the client's bounded output queue.
 *
 * The queue is compacted before appending. If it still lacks space, the caller
 * treats the client as too slow or broken and closes it rather than allowing
 * memory use to grow without bound. This function never performs socket I/O.
 */
static int queue_packet(Client *client, uint8_t type,
			const char *payload, size_t length)
{
	uint32_t network_length;
	size_t used;
	size_t packet_length;

	if (client->fd == -1 || length > CHAT_MAX_FRAME - 1)
		return -1;
	if (length > 0 &&
	    (payload == NULL || memchr(payload, '\0', length) != NULL))
		return -1;

	if (client->output_start > 0) {
		used = client->output_end - client->output_start;
		if (used > 0)
			memmove(client->output,
				client->output + client->output_start, used);
		client->output_start = 0;
		client->output_end = used;
	}

	packet_length = 4 + 1 + length;
	if (client->output_end > CLIENT_OUTPUT_SIZE - packet_length)
		return -1;

	network_length = htonl((uint32_t)length + 1);
	memcpy(client->output + client->output_end, &network_length,
	       sizeof(network_length));
	client->output_end += sizeof(network_length);
	client->output[client->output_end++] = type;
	if (length > 0) {
		memcpy(client->output + client->output_end, payload, length);
		client->output_end += length;
	}
	return 0;
}

/*
 * Release a session and optionally inform remaining users. Reset the slot before
 * broadcasting so the departing client cannot receive its own leave notice.
 * The caller must not use the client pointer after the nested broadcast returns.
 */
static void drop_client(Client *client, int announce)
{
	char message[CHAT_MAX_NAME + 32];
	char name[CHAT_MAX_NAME + 1];
	int was_authenticated;
	int written;

	if (client->fd == -1)
		return;

	was_authenticated = client->authenticated;
	memcpy(name, client->name, sizeof(name));
	printf("client(fd=%d%s%s) disconnected\n", client->fd,
	       was_authenticated ? ", name=" : "",
	       was_authenticated ? name : "");

	shutdown(client->fd, SHUT_RDWR);
	close(client->fd);
	reset_client(client);

	if (!announce || !was_authenticated)
		return;

	written = snprintf(message, sizeof(message), "%s left the chat", name);
	if (written <= 0 || (size_t)written >= sizeof(message))
		return;
	broadcast_packet(client, CHAT_MSG_TEXT, message, (size_t)written);
}

/*
 * Queue a message for every other authenticated session. A queue failure means
 * that recipient has not drained enough output, so disconnect it immediately.
 */
static void broadcast_packet(Client *sender, uint8_t type,
			     const char *payload, size_t length)
{
	int slot;

	for (slot = 0; slot < MAX_CLIENTS; slot++) {
		Client *client = &clients[slot];

		if (client == sender || client->fd == -1 ||
		    !client->authenticated)
			continue;
		if (queue_packet(client, type, payload, length) != 0)
			drop_client(client, 1);
	}
}

/* Names are reserved only by authenticated sessions currently in the table. */
static int name_is_used(const char *name)
{
	int slot;

	for (slot = 0; slot < MAX_CLIENTS; slot++) {
		if (clients[slot].fd != -1 &&
		    clients[slot].authenticated &&
		    strcasecmp(clients[slot].name, name) == 0)
			return 1;
	}
	return 0;
}

/*
 * Names are opaque byte strings, normally UTF-8. Leading/trailing whitespace
 * and ASCII control characters are rejected to keep prompts readable.
 */
static int name_is_valid(const char *name, size_t length)
{
	size_t index;

	if (length == 0 || length > CHAT_MAX_NAME)
		return 0;
	if (isspace((unsigned char)name[0]) ||
	    isspace((unsigned char)name[length - 1]))
		return 0;

	for (index = 0; index < length; index++) {
		unsigned char character = (unsigned char)name[index];

		if (character < 0x20 || character == 0x7f)
			return 0;
	}
	return 1;
}

static int send_login_error(Client *client, const char *message)
{
	return queue_packet(client, CHAT_MSG_LOGIN_ERROR, message,
			    strlen(message));
}

/*
 * Enforce the per-connection state machine:
 * unauthenticated -> login request -> authenticated -> text messages.
 * A failed login leaves the connection open so the client can retry a name.
 */
static int handle_packet(Client *client, uint8_t type,
			 const char *payload, size_t length)
{
	if (type == CHAT_MSG_LOGIN) {
		char name[CHAT_MAX_NAME + 1];
		char notice[CHAT_MAX_NAME + 32];
		int written;

		if (client->authenticated)
			return send_login_error(client, "Already logged in.");
		if (!name_is_valid(payload, length))
			return send_login_error(
				client, "Name must be 1-32 printable characters.");

		memcpy(name, payload, length);
		name[length] = '\0';
		if (name_is_used(name))
			return send_login_error(
				client, "That name is already in use.");

		/* Commit authentication only after all validation and queueing succeed. */
		memcpy(client->name, name, length + 1);
		client->authenticated = 1;
		if (queue_packet(client, CHAT_MSG_LOGIN_OK, name, length) != 0)
			return -1;

		printf("client(fd=%d) logged in as %s\n", client->fd, name);
		written = snprintf(notice, sizeof(notice), "%s joined the chat",
				   name);
		if (written > 0 && (size_t)written < sizeof(notice))
			broadcast_packet(client, CHAT_MSG_TEXT, notice,
					 (size_t)written);
		return 0;
	}

	if (!client->authenticated)
		return send_login_error(client,
					"Log in before sending messages.");

	if (type != CHAT_MSG_TEXT)
		return -1;
	if (length == 0 || length > CHAT_MAX_TEXT)
		return -1;

	{
		char message[CHAT_MAX_FRAME];
		int written = snprintf(message, sizeof(message), "%s: %.*s",
				       client->name, (int)length, payload);

		if (written <= 0 || (size_t)written >= sizeof(message))
			return -1;
		broadcast_packet(client, CHAT_MSG_TEXT, message,
				 (size_t)written);
	}
	return 0;
}

/*
 * Consume every complete frame currently in the input buffer. Incomplete data
 * stays for the next poll iteration; this is what supports TCP fragmentation and
 * coalescing. A malformed frame terminates the connection.
 */
static int process_client_frames(Client *client)
{
	for (;;) {
		uint32_t network_length;
		size_t frame_length;
		uint8_t type;
		const char *payload;
		size_t payload_length;

		if (client->input_length < 4)
			return 0;

		memcpy(&network_length, client->input, sizeof(network_length));
		network_length = ntohl(network_length);
		if (network_length < 1 || network_length > CHAT_MAX_FRAME)
			return -1;
		frame_length = 4 + (size_t)network_length;
		if (client->input_length < frame_length)
			return 0;

		type = client->input[4];
		payload = (const char *)client->input + 5;
		payload_length = (size_t)network_length - 1;
		if (memchr(payload, '\0', payload_length) != NULL)
			return -1;
		if (handle_packet(client, type, payload, payload_length) != 0)
			return -1;

		/* Preserve bytes belonging to the next frame, if any. */
		memmove(client->input, client->input + frame_length,
			client->input_length - frame_length);
		client->input_length -= frame_length;
	}
}

/*
 * Read once from the socket and parse all newly completed frames. EAGAIN is
 * expected on a nonblocking descriptor and simply ends this readiness pass.
 */
static int read_client(Client *client)
{
	ssize_t received;

	if (client->input_length == sizeof(client->input))
		return -1;

	received = recv(client->fd, client->input + client->input_length,
			sizeof(client->input) - client->input_length, 0);
	if (received == 0)
		return -1;
	if (received < 0) {
		if (errno == EINTR || errno == EAGAIN ||
		    errno == EWOULDBLOCK)
			return 0;
		return -1;
	}

	client->input_length += (size_t)received;
	return process_client_frames(client);
}

/*
 * Write once from the queued output. Partial writes retain the rest for a later
 * POLLOUT event; EAGAIN means the queue remains pending without an error.
 */
static int send_client_output(Client *client)
{
	ssize_t sent;

	if (client->output_start == client->output_end)
		return 0;

	sent = send(client->fd, client->output + client->output_start,
		    client->output_end - client->output_start, MSG_NOSIGNAL);
	if (sent < 0) {
		if (errno == EINTR || errno == EAGAIN ||
		    errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
	if (sent == 0)
		return -1;

	client->output_start += (size_t)sent;
	if (client->output_start == client->output_end) {
		client->output_start = 0;
		client->output_end = 0;
	}
	return 0;
}

/* Drain all pending connections so a burst of clients does not wait for poll. */
static void accept_clients(int server_fd)
{
	for (;;) {
		struct sockaddr_in client_address;
		socklen_t client_address_length = sizeof(client_address);
		int client_fd;
		int slot;

		client_fd = accept(server_fd,
				   (struct sockaddr *)&client_address,
				   &client_address_length);
		if (client_fd == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			perror("accept");
			return;
		}

		for (slot = 0; slot < MAX_CLIENTS; slot++) {
			if (clients[slot].fd == -1)
				break;
		}
		if (slot == MAX_CLIENTS) {
			printf("connection rejected: server is full\n");
			close(client_fd);
			continue;
		}
		if (set_nonblocking(client_fd) != 0) {
			perror("fcntl");
			close(client_fd);
			continue;
		}

		reset_client(&clients[slot]);
		clients[slot].fd = client_fd;
		printf("client connected from %s:%d (fd=%d)\n",
		       inet_ntoa(client_address.sin_addr),
		       ntohs(client_address.sin_port), client_fd);
	}
}

int run_server(void)
{
	struct sockaddr_in server_address;
	struct pollfd fds[MAX_CLIENTS + 1];
	int server_fd;
	int reuse = 1;
	int slot;

	/* fd == -1 is the canonical "slot free" representation. */
	for (slot = 0; slot < MAX_CLIENTS; slot++)
		reset_client(&clients[slot]);

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
	if (set_nonblocking(server_fd) != 0) {
		perror("fcntl");
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

	printf("chat server listening on port %d\n", CHAT_PORT);
	fflush(stdout);

	/*
	 * The first pollfd is the listener; the remaining entries mirror the client
	 * slot table. Events are built from the current snapshot and processed in
	 * this single thread, so callbacks and broadcasts need no synchronization.
	 */
	for (;;) {
		int ready;

		memset(fds, 0, sizeof(fds));
		fds[0].fd = server_fd;
		fds[0].events = POLLIN;
		for (slot = 0; slot < MAX_CLIENTS; slot++) {
			fds[slot + 1].fd = clients[slot].fd;
			if (clients[slot].fd == -1)
				continue;
			fds[slot + 1].events = POLLIN;
			if (clients[slot].output_start !=
			    clients[slot].output_end)
				fds[slot + 1].events |= POLLOUT;
		}

		ready = poll(fds, MAX_CLIENTS + 1, -1);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			close(server_fd);
			return -1;
		}

		if (fds[0].revents & POLLIN)
			accept_clients(server_fd);
		if (fds[0].revents & (POLLERR | POLLNVAL))
			break;

		/*
		 * Read before write so newly arrived commands can enqueue responses in
		 * the same iteration. Each handler may remove its client, so verify the
		 * descriptor after every call before touching the session again.
		 */
		for (slot = 0; slot < MAX_CLIENTS; slot++) {
			Client *client = &clients[slot];
			short revents = fds[slot + 1].revents;

			if (client->fd == -1)
				continue;
			if (revents & (POLLERR | POLLNVAL)) {
				drop_client(client, 1);
				continue;
			}
			if ((revents & POLLIN) && read_client(client) != 0) {
				drop_client(client, 1);
				continue;
			}
			if (client->fd != -1 && (revents & POLLOUT) &&
			    send_client_output(client) != 0) {
				drop_client(client, 1);
				continue;
			}
			if (client->fd != -1 && (revents & POLLHUP))
				drop_client(client, 1);
		}
	}

	for (slot = 0; slot < MAX_CLIENTS; slot++)
		drop_client(&clients[slot], 0);
	close(server_fd);
	return -1;
}

int main(void)
{
	return run_server() == 0 ? 0 : 1;
}
