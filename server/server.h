#ifndef SERVER_H
#define SERVER_H

#include "../protocol.h"

#define MAX_CLIENTS 10
#define CLIENT_INPUT_SIZE (CHAT_MAX_FRAME + 4)
#define CLIENT_OUTPUT_SIZE (64 * 1024)

int run_server(void);

#endif
