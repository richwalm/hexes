CC=gcc
CFLAGS=-pedantic -Wall -O2 -s -I../include
LDFLAGS=-L../lib
LDLIBS=-lhexes

.PHONY: all clean

OBJS=bullets.o keys.o badapple.o

all: bullets keys badapple

badapple: badapple.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -lz -o $@

clean:
	rm -rf bullets keys badapple $(OBJS)
