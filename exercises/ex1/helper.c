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
