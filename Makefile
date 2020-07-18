CC=gcc
CFLAGS=-Wall -g
LDFLAGS=-pthread

all: server

server: server.c

clean:
	rm -f server
.PHONY: clean
