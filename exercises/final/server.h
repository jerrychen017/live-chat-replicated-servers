#ifndef SERVER_H
#define SERVER_H

struct room {
    char name[80];
    struct participant* participants[5];
    struct message *messages;
    struct room *next;
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

struct room* create_room(struct room** rooms_ref, char* room_name);
struct room* find_room(struct room* rooms, char* room_name);
struct room* find_room_of_client(struct room* rooms, char* client_name, int server_index);
bool find_client(struct participant* list, char* client_name);
int add_client(struct room* room, char* client_name, int server_index);
int remove_client(struct room* room, char* client_name, int server_index);
void get_messages(char* to_send, struct room* room);
void get_participants(char* to_send, struct room* room);

#endif