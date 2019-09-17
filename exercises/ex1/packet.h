#ifndef PACKET_H
#define PACKET_H

#define WINDOW_SIZE = 16;
#define PACKET_SIZE = 100;
#define BUF_SIZE = PACKET_SIZE - sizeof(unsigned char) - sizeof(unsigned int);

struct packet {
    unsigned char tag;
    unsigned int sequence;
    char[BUF_SIZE] file;
}

#endif
