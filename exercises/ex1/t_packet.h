#ifndef T_PACKET_H
#define T_PACKET_H

#define MAX_PACKET_SIZE 1400
#define PACKET_SIZE MAX_PACKET_SIZE
#define BUF_SIZE PACKET_SIZE - 2 * sizeof(unsigned int)

#define FILENAME 0
#define DATA 1
#define END 2

struct t_packet {

    unsigned int tag;
    unsigned int bytes;
    char file[BUF_SIZE];
};

#endif
