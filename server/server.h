#ifndef SERVER_H
#define SERVER_H

#include "../protocol.h"

/* Fixed-size session table used by the single poll loop. */
#define MAX_CLIENTS 10
/* Room for the largest frame plus its four-byte length prefix. */
#define CLIENT_INPUT_SIZE (CHAT_MAX_FRAME + 4)
/* Bounded per-client output queue; slow readers are disconnected on overflow. */
#define CLIENT_OUTPUT_SIZE (64 * 1024)

/* Initialize the listening socket and run until an unrecoverable error occurs. */
int run_server(void);

#endif
