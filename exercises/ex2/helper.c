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
                     unsigned int start_array_index, int buffer_size)
{
  return (packet_index - start_packet_index + start_array_index) % buffer_size;
}

bool check_finished_delivery(bool *finished, int * last_counters, int num_machines, int machine_index, int counter) {
    bool is_finished = true; 
    for (int i = 0; i < num_machines; i++) {
        is_finished = is_finished && finished[i];
    }
    
    if (is_finished) {
        last_counters[machine_index - 1] = counter;
    }
    return is_finished; 
}

bool check_acks(int * acks, int num_machines, int num_packets) { 
    // min(ack) == num_packets (all other machines delivered my packets)
    int min_ack = acks[0]; 
    for (int i = 0; i < num_machines; i++) { 
        if (acks[i] < min_ack) {
            min_ack = acks[i];
        }
    }
    return min_ack == num_packets;
}

void print_status(int *acks, int *start_array_indices, 
    int *start_packet_indices, int *end_indices, bool* finished, int *last_counters,
    int counter, int last_delivered_counter, int num_created, int machine_index, int num_machines) {

    printf("-------------------------------STATUS report-------------------------------\n");
    // created packet
    printf("Counter: %d\n", counter);
    printf("----Created packets----\n");
    printf("Created %d packets\n", num_created);
    printf("start packet index: %d\n", start_packet_indices[machine_index - 1]);
    printf("start array index: %d\n", start_array_indices[machine_index - 1]);
    
    // acks
    printf("-------Ack-------\n");
    for (int i = 0; i < num_machines; i++) {
        printf("machine %d has delivered my packets up to %d\n", i + 1, acks[i]);
    }

    // table
    printf("------Table------\n");
    printf("Last delivered counter: %d\n", last_delivered_counter);
    for (int i = 0; i < num_machines; i++) {
        if (i + 1 != machine_index) {
            printf("machine %d:\n", i + 1);
            printf("start packet index: %d\n", start_packet_indices[i]);
            printf("start array index: %d\n", start_array_indices[i]);
        }
    }

    // end_indices
    printf("------End indices------\n");
    for (int i = 0; i < num_machines; i++) {
        if (end_indices[i] == -1) {
            printf("machine %d last packet index: unknown\n", i + 1);
        } else {
            printf("machine %d last packet index: %d\n", i + 1, end_indices[i]);
        }
    }

    // finished
    printf("------Finished------\n");
    for (int i = 0; i < num_machines; i++) {
        if (finished[i]) {
            printf("machine %d: finished\n", i + 1);
        } else {
            printf("machine %d: not done\n", i + 1);
        }
    }

    // last_counters 
    printf("------Last Counters------\n");
    for (int i = 0; i < num_machines; i++) {
        printf("machine %d: last_counter is %d\n", i + 1, last_counters[i]);
        
    }

    printf("------------------------------STATUS report END------------------------------\n\n");

}