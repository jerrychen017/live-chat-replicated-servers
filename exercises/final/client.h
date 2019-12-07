#ifndef CLIENT_H
#define CLIENT_H

struct message {
    int counter;
    int server_index;
    char creator[80];
    char content[80];
    int num_likes;
    struct message* next;
};

struct participant {
    char name[80];
    struct participant* next;
};

void connection_timeout_event(int code, void * data);

#endif