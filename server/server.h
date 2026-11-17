/***********************************
 * server.h                        *
 * the header files and functions  *
 ***********************************/
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <signal.h>
#include <stdio.h> 
#include <errno.h> 
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#define bool _Bool
#define true 1
#define false 0
#define PORT 8000
#define MAXSIZE 1024
int build_socket();
void send_func(const char *);
void *recv_func(int *_clientfd);
