client:client.c cl_sock.c client.h
	gcc -Wall -o client client.c cl_sock.c `pkg-config gtk+-2.0 --libs --cflags` -lpthread

clean:
	rm client
