CC ?= gcc
CFLAGS ?= -Wall -Wextra -g

server: server.c server.h ../protocol.c ../protocol.h
	$(CC) $(CFLAGS) -I.. -o $@ server.c ../protocol.c -lpthread

clean:
	rm -f server

.PHONY: clean
