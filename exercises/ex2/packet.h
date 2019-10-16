#ifndef PACKET_H
#define PACKET_H

#define WINDOW_SIZE 5

#define TAG_START 0
#define TAG_DATA 1
#define TAG_ACK 2
#define TAG_NACK 3
#define TAG_END 4
#define TAG_EMPTY 5
#define TAG_COUNTER 6 // message that contains the last counter this machine delivered 

#define DELIVERY_GAP 5

struct packet {
    unsigned int tag;
    unsigned int counter;
    unsigned int machine_index; // starts from 1
    unsigned int packet_index; // starts from 1
    unsigned int random_data;
    /*
    if tag == TAG_ACK, first <num_machine> integers represent corresponding ack value
    if tag == TAG_NACK, first <num_machine> integers represent corresponding nack packet_index. 
    if the value is -1, then there's no nack for that machine.
    */
    int payload[1400 / sizeof(int)];
};

#endif