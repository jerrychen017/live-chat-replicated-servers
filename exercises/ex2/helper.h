#ifndef HELPER_H
#define HELPER_H

/* Performs left - right and returns the result as a struct timeval.
 * In case of negative result (right > left), zero elapsed time is returned
 */
struct timeval diffTime(struct timeval left, struct timeval right);

/* Convert current sequence to corresponding index in the window
 * since smallest sequence starts at start_index
 */
unsigned int convert(unsigned int sequence, unsigned int start_sequence, unsigned int start_index);

// Print packet information for debugging
void print_packet(struct packet *to_print, int num_machines);

void print_status(struct packet *created_packets, int *acks, int *start_array_indices, 
    int *start_packet_indices, int *end_indices, bool* finished, int *last_counters,
    int counter, int last_delivered_counter, int num_created, int machine_index, int num_machines);

// checks if my machine has finished delivering packets of all machines
bool check_finished_delivery(bool *finished, int * last_counters, int num_machines, int machine_index, int counter);

bool check_acks(int * acks, int num_machines, int num_packets);

/*
* Slide windows
*/


#endif
