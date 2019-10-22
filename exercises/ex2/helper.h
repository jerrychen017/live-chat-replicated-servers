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

/* Checks if my machine has finished delivering packets of all machines
 * if finished, update last_counter of this machine
 */
bool check_finished_delivery(bool *finished, int * last_counters, int num_machines, int machine_index, int counter);

/* Checks if all other machines have finished delivering all my packets
 */
bool check_acks(int * acks, int num_machines, int num_packets);


#endif
