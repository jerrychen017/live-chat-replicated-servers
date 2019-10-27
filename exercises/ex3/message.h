#ifndef MESSAGE_H
#define MESSAGE_H

#define TAG_DATA 0 
#define TAG_END 1

struct message {
    unsigned int tag;
    unsigned int process_index; // starts from 1
    unsigned int message_index; // starts from 1
    unsigned int random_number;
    int payload[1300 / sizeof(int)];
};

/**
 Tuning hyperparameters
*/

#define INIT_SEND_SIZE 10



#endif 