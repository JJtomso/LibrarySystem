CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LIBS = -lmicrohttpd -lpthread

all:
	$(CC) $(CFLAGS) -o server server.c $(LIBS)

clean:
	rm -f server