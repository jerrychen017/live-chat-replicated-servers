CC=gcc

CFLAGS = -g -c -Wall -pedantic

all: test ncp rcv

test: test.o sendto_dbg.o
	    $(CC) -o test test.o sendto_dbg.o   

ncp: ncp.o
	    $(CC) -o ncp ncp.o sendto_dbg.o

rcv: rcv.o
	    $(CC) -o rcv rcv.o sendto_dbg.o

sendto_dbg.o: sendto_dbg.c sendto_dbg.h
	    $(CC) $(CFLAGS) sendto_dbg.c 

ncp.o: ncp.c sendto_dbg.h
	    $(CC) $(CFLAGS) ncp.c

rcv.o: rcv.c sendto_dbg.h packet.h net_include.h
		$(CC) $(CFLAGS) rcv.c

clean:
	rm *.o
	rm ncp
	rm rcv
	rm test

# COMMENT OUT FOR NOW
#%.o:    %.c
#	$(CC) $(CFLAGS) $*.c
