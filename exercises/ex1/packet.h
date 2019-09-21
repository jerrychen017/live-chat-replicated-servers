#ifndef PACKET_H
#define PACKET_H

#define MAX_PACKET_SIZE 1400
#define WINDOW_SIZE 1024
#define PACKET_SIZE MAX_PACKET_SIZE
#define BUF_SIZE PACKET_SIZE - 3 * sizeof(unsigned int)
#define MAX_NUMS_NACK (PACKET_SIZE - 3 * sizeof(unsigned int)) / sizeof(unsigned int)
#define NACK_SIZE 50

struct packet {
  unsigned int tag;
  unsigned int sequence;
  unsigned int bytes;
  char file[BUF_SIZE];
};

struct packet_mess {
  unsigned int tag;
  unsigned int ack;        // representing sequence
  unsigned int nums_nack;  // number of nacks
  unsigned int nack[NACK_SIZE];
};

#endif
