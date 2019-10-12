CC=gcc

CFLAGS = -g -c -Wall -pedantic -D_GNU_SOURCE

all: mcast start_mcast

mcast: mcast.o helper.o
	$(CC) -o mcast mcast.o helper.o

start_mcast: start_mcast.o
	$(CC) -o start_mcast start_mcast.o 

mcast.o: mcast.c net_include.h packet.h helper.h
	$(CC) $(CFLAGS) mcast.c

start_mcast.o: start_mcast.c net_include.h packet.h
	$(CC) $(CFLAGS) start_mcast.c

helper.o: helper.c helper.h net_include.h
	$(CC) $(CFLAGS) helper.c

clean:
	rm *.o
	rm mcast
	rm start_mcast
