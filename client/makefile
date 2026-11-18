CC ?= gcc
CFLAGS ?= -Wall -Wextra -g
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-2.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-2.0)

client: client.c cl_sock.c client.h ../protocol.c ../protocol.h
	$(CC) $(CFLAGS) -I.. -o $@ client.c cl_sock.c ../protocol.c \
		$(GTK_CFLAGS) $(GTK_LIBS) -lpthread

clean:
	rm -f client

.PHONY: clean
