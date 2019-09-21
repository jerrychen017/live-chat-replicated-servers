#include "net_include.h"
#include "packet.h"
#include "helper.h"

struct timeval diffTime(struct timeval left, struct timeval right)
{
    struct timeval diff;

    diff.tv_sec  = left.tv_sec - right.tv_sec;
    diff.tv_usec = left.tv_usec - right.tv_usec;

    if (diff.tv_usec < 0) {
        diff.tv_usec += 1000000;
        diff.tv_sec--;
    }

    if (diff.tv_sec < 0) {
        printf("WARNING: diffTime has negative result, returning 0!\n");
        diff.tv_sec = diff.tv_usec = 0;
    }

    return diff;
}


unsigned int convert(unsigned int sequence, unsigned int start_sequence,
                     unsigned int start_index)
{
  return (sequence - start_sequence + start_index) % WINDOW_SIZE;
}


void print_sent_packet(struct packet_mess* packet_sent)
{

    printf("Sent packet with ack %d, nums_nack %d, nacks: ", packet_sent->ack, packet_sent->nums_nack);
    for (int i = 0; i < packet_sent->nums_nack; i++) {
        printf("%d ", packet_sent->nack[i]);
    }
    printf("\n");
}


void print_received_packet(struct packet* packet_received)
{
    printf("Receive packet with sequence %d, bytes %d\n", packet_received->sequence, packet_received->bytes);
}
