CC=gcc

CFLAGS = -g -c -Wall -pedantic

all: test

test: test.o sendto_dbg.o
	    $(CC) -o test test.o sendto_dbg.o  

ncp: ncp.o 
		$(CC) -o ncp ncp.o

rcv: rcv.o
		$(CC) -o rcv rcv.o

clean:
	rm *.o
	rm ncp
	rm rcv
	rm test

%.o:    %.c
	$(CC) $(CFLAGS) $*.c
