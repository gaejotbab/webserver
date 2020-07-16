CC=gcc
CFLAGS=-Wall
LDFLAGS=-pthread

all: server

server: server.c

clean:
	rm -f server
.PHONY: clean
