CC ?= gcc
CFLAGS ?= -Wall -Wextra -g

server: server.c server.h ../protocol.h
	$(CC) $(CFLAGS) -I.. -o $@ server.c

clean:
	rm -f server

.PHONY: clean
