CC=gcc
LD=gcc
CFLAGS=-g -Wall 
CPPFLAGS=-I. -I/home/cs417/exercises/ex3/include
SP_LIBRARY=/home/cs417/exercises/ex3/libspread-core.a /home/cs417/exercises/ex3/libspread-util.a

all: chat_client chat_server

chat_client: chat_client.o
	$(LD) -o $@ chat_client.o -ldl $(SP_LIBRARY)

chat_client.o: chat_client.c message.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c chat_client.c

chat_server: chat_server.o
	$(LD) -o $@ chat_server.o -ldl $(SP_LIBRARY)

chat_server.o: chat_server.c message.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c chat_server.c

clean:
	rm *.o
	rm chat_server chat_client
