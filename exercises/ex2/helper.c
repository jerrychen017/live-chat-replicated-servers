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

void check_end(FILE *fd, int *acks, bool *finished, int num_machines, int machine_index, int num_packets)
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
        fclose(fd);
        printf("Ready to end\n");
    }
}

void print_packet(struct packet *to_print, int num_machines) {
    
    char divider[] = "----------------------------------------------------------------------";

    switch(to_print->tag) {
        case TAG_START:
        {
            printf(" ** ERROR ** ");
            printf("Receive START packet\n");
            printf("%s\n", divider);
            break;
        }

        case TAG_DATA:
        {
            printf("Receive DATA packet\n");
            printf("counter: %d\n", to_print->counter);
            printf("from machine: %d\n", to_print->machine_index);
            printf("packet index: %d\n", to_print->packet_index);
            printf("random data: %d\n", to_print->random_data);
            printf("%s\n", divider);
            break;
        }

        case TAG_ACK:
        {
            printf("Receive ACK packet\n");
            for (int i = 0; i < num_machines; i++) {
                printf("machine %d ack: %d\n", i + 1, to_print->payload[i]);
            }
            printf("%s\n", divider);
            break;
        }

        case TAG_NACK:
        {
            printf("Receive NACK packet\n");
            for (int i = 0; i < num_machines; i++) {
                if (to_print->payload[i] == -1) {
                    printf("machine %d nack: none\n", i + 1);
                } else {
                    printf("machine %d nack: %d\n", i + 1, to_print->payload[i]);
                }
            }
            printf("%s\n", divider);
            break;
        }

        case TAG_END:
        {
            printf("Receive END packet\n");
            printf("from machine: %d\n", to_print->machine_index);
            printf("last packet index: %d\n", to_print->packet_index);
            printf("%s\n", divider);
            break;
        }

        case TAG_EMPTY:
        {
            printf(" ** ERROR ** ");
            printf("Receive EMPTY packet\n");
            printf("%s\n", divider);
            break;
        }

    }
}

void print_status(struct packet *created_packets, int *acks, struct packet *table[], int *start_array_indices, 
    int *start_packet_indices, int *end_indices, bool* finished,
    int counter, int last_delivered_counter, int num_created, int machine_index, int num_machines) {

    char divider[] = "----------------------------------------------------------------------";
    printf("--------STATUS report--------\n");
    // created packet
    printf("----Created packets----\n");
    printf("Created %d packets\n", num_created);
    printf("start packet index: %d\n", start_packet_indices[machine_index - 1]);
    printf("start array index: %d\n", start_array_indices[machine_index - 1]);
    printf("packets:\n");
    for (int i = 0; i < WINDOW_SIZE; i++) {
        printf("array index: %d, ", i);
        if (created_packets[i].tag == TAG_EMPTY) {
            printf("empty\n");
            continue;
        }
        printf("counter: %d, ", created_packets[i].counter);
        printf("from machine: %d, ", created_packets[i].machine_index);
        printf("packet index: %d, ", created_packets[i].packet_index);
        printf("random data: %d\n", created_packets[i].random_data);
    }

    // acks
    printf("-------Ack-------\n");
    for (int i = 0; i < num_machines; i++) {
        printf("machine %d delivered up to %d\n", i + 1, acks[i]);
    }

    // table
    printf("------Table------\n");
    printf("Last delivered counter: %d\n", last_delivered_counter);
    for (int i = 0; i < num_machines; i++) {
        if (i + 1 != machine_index) {
            printf("machine %d:\n", i + 1);
            printf("start packet index: %d\n", start_packet_indices[i]);
            printf("start array index: %d\n", start_array_indices[i]);
            for (int j = 0; j < WINDOW_SIZE; j++) {
                printf("    array index: %d, ", j);
                if (table[i][j].tag == TAG_EMPTY) {
                    printf("empty\n");
                    continue;
                }
                printf("    counter: %d, ", created_packets[i].counter);
                printf("    from machine: %d, ", created_packets[i].machine_index);
                printf("    packet index: %d, ", created_packets[i].packet_index);
                printf("    random data: %d\n", created_packets[i].random_data);
            }
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
            printf("machine %d finished\n", i + 1);
        }
    }

    printf("%s\n", divider);
}