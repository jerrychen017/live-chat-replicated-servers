#ifndef PACKET_H
#define PACKET_H

#define WINDOW_SIZE 16
#define PACKET_SIZE 100
#define BUF_SIZE PACKET_SIZE - sizeof(unsigned char) - sizeof(unsigned int)

struct packet {
    unsigned char tag;
    unsigned int sequence;
    char file[BUF_SIZE];
};

struct packet_ack {
    unsigned char tag;
    unsigned char ack;
    unsigned char nums_nack;
    unsigned int nack[WINDOW_SIZE];
};

#endif
