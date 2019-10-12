#ifndef PACKET_H
#define PACKET_H

#define WINDOW_SIZE 5
// (10 + 10 * WINDOW_SIZE) * sizeof(int) < 1400
#define MAX_WINDOW_SIZE (1400 / sizeof(int) - 10) / 10

#define TAG_START 0
#define TAG_DATA 1
#define TAG_ACK 2
#define TAG_NACK 3
#define TAG_END 4

#define DELIVERY_GAP 5

struct packet {
    unsigned int tag;
    unsigned int counter;
    unsigned int machine_index;
    unsigned int packet_index;
    unsigned int random_data;
    /*
    if tag == TAG_ACK, first <num_machine> integers represents corresponding ack value
    if tag == TAG_NACK, first <num_machine> integers represents # of nacks,
                    followed with index of indiviual nacks
    */
    int payload[1400 / sizeof(int)];
};

#endif