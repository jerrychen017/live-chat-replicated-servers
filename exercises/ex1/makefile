CC=gcc

CFLAGS = -g -c -Wall -pedantic

all: test ncp rcv t_rcv t_ncp

test: test.o sendto_dbg.o
	    $(CC) -o test test.o sendto_dbg.o   

ncp: ncp.o sendto_dbg.o helper.o
	    $(CC) -o ncp ncp.o sendto_dbg.o helper.o

rcv: rcv.o sendto_dbg.o helper.o
	    $(CC) -o rcv rcv.o sendto_dbg.o helper.o

t_rcv: t_rcv.o helper.o
		$(CC) -o t_rcv t_rcv.o helper.o

t_ncp: t_ncp.o helper.o
		$(CC) -o t_ncp t_ncp.o helper.o

sendto_dbg.o: sendto_dbg.c sendto_dbg.h
	    $(CC) $(CFLAGS) sendto_dbg.c 

ncp.o: ncp.c sendto_dbg.h packet.h net_include.h tag.h helper.h
	    $(CC) $(CFLAGS) ncp.c

rcv.o: rcv.c sendto_dbg.h packet.h net_include.h tag.h helper.h
		$(CC) $(CFLAGS) rcv.c

helper.o: helper.c helper.h net_include.h
		$(CC) $(CFLAGS) helper.c

t_rcv.o: t_rcv.c net_include.h packet.h helper.h
		$(CC) $(CFLAGS) t_rcv.c

t_ncp.o: t_ncp.c net_include.h packet.h helper.h
		$(CC) $(CFLAGS) t_ncp.c

clean:
	rm *.o
	rm ncp
	rm rcv
	rm test
	rm t_ncp
	rm t_rcv
