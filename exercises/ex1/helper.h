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

#endif
