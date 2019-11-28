CC=gcc
LD=gcc
CFLAGS=-g -Wall 
CPPFLAGS=-I. -I/home/cs417/exercises/ex3/include
SP_LIBRARY=/home/cs417/exercises/ex3/libspread-core.a /home/cs417/exercises/ex3/libspread-util.a

all: client server

client: client.o
	$(LD) -o $@ client.o -ldl $(SP_LIBRARY)

server: server.o
	$(LD) -o $@ server.o -ldl $(SP_LIBRARY)

client.o: client.c config.h client.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c client.c

server.o: server.c config.h server.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c server.c

clean:
	rm *.o
	rm server client
	rm *.out
