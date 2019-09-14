CC=gcc

CFLAGS = -g -c -Wall -pedantic

all: test

test: test.o sendto_dbg.o
	    $(CC) -o test test.o sendto_dbg.o  

clean:
	rm *.o
	rm test

%.o:    %.c
	$(CC) $(CFLAGS) $*.c
