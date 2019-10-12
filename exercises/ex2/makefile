CC=gcc

CFLAGS = -g -c -Wall -pedantic -D_GNU_SOURCE

all: mcast

mcast: mcast.o
	$(CC) -o mcast mcast.o 

mcast.o: mcast.c net_include.h
	$(CC) $(CFLAGS) mcast.c

clean:
	rm *.o
	rm mcast
