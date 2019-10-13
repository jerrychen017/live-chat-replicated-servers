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

/* Known end condition:
*  1. Other machines have received all my packets (min(ack) == num_packets)
*  2. Deliver all packets from other machines
*/
void check_end(FILE *fd, int *acks, bool *finished, int num_machines, int machine_index, int num_packets);

#endif
