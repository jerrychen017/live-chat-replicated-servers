#ifndef SERVER_H
#define SERVER_H

struct room {
    char name[80];
    struct participant* participants[5];
    struct message *messages;
};

struct participant {
    char name[80];
    struct participant* next;
};

struct message {
    int timestamp;
    int server_index;
    char content[80];
    char creator[80];
    struct participant* liked_by;
    struct message* next;
};

struct log {
    int timestamp;
    char content[300];
    struct log* next;
};

struct room* find_room(struct room* rooms, char* room_name);
void get_messages(char* to_send, struct room* room);
#endif