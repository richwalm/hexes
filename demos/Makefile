CC=gcc
CFLAGS=-pedantic -Wall -O2 -s -I../include
LDFLAGS=-L../lib
LDLIBS=-lhexes

.PHONY: all clean

OBJS=bullets.o keys.o

all: bullets keys

clean:
	rm -rf bullets keys $(OBJS)
