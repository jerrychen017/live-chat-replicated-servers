#ifndef PACKET_H
#define PACKET_H

#define TAG_START 0
#define TAG_DATA 1
#define TAG_ACK 2
#define TAG_NACK 3
#define TAG_END 4
#define TAG_COUNTER 5 // message that contains the last counter this machine delivered 

/**
 * Tuning hyperparameters
 */
#define TABLE_SIZE 400
#define FRACTION_TO_SEND 1
#define FRACTION_DELIVERY_GAP 10
#define TIMEOUT_SEC 1
#define TIMEOUT_USEC 0
#define RETRANSMIT_INTERVAL_SEC 1
#define RETRANSMIT_INTERVAL_USEC 0
#define NUM_EXIT_SIGNALS 5
#define CREATED_PACKETS_SIZE 20
#define ACK_GAP 5

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
    if tag == TAG_COUNTER, first <num_machine> integers represent last counters for each machine.
    */
    int payload[1400 / sizeof(int)];
};

#endif