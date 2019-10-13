#include <stdbool.h>
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


unsigned int convert(unsigned int packet_index, unsigned int start_packet_index,
                     unsigned int start_array_index)
{
  return (packet_index - start_packet_index + start_array_index) % WINDOW_SIZE;
}

void check_end(int *acks, bool *finished, int num_machines, int machine_index, int num_packets)
{
    // all finished array entries are true 
    for (int i = 0; i < num_machines; i++) {
        if (i == machine_index - 1) {
            continue; 
        }
        if (!finished[i]) {
            return; 
        } 
    }

    // min(ack) == num_packets (all other machines received my packets)
    int min_ack = acks[0]; 
    for (int i = 0; i < num_machines; i++) { 
        if (acks[i] < min_ack) {
            min_ack = acks[i];
        }
    }
    if (min_ack == num_packets) {
        printf("Ready to end\n");
    }
}
