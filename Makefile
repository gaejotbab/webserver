CC=gcc
CFLAGS=-Wall -g
LDFLAGS=-pthread

all: server
.PHONY: all

server: server.c

clean:
	rm -f server
.PHONY: clean
