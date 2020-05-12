CFLAGS=-std=c99 -g -Wall
LOADLIBES=-pthread -lm

.PHONY: all

all: client server
