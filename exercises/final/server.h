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
    int counter;
    int server_index;
    char content[81];
    char creator[80];
    struct like* likes;
    struct message* next;
};

struct like {
    char username[80];
    bool liked;
    int counter;
    int server_index;
    struct like *next;
};

struct log {
    int counter;
    int server_index;
    int index;
    char content[300];
    struct log* next;
};

struct room* create_room(struct room** rooms_ref, char* room_name);
struct room* find_room(struct room* rooms, char* room_name);
struct room* find_room_of_client(struct room* rooms, char* client_name, int server_index);
bool find_client(struct participant* list, char* client_name);
void clear_client(struct room* room, int server_index);
int add_client(struct room* room, char* client_name, int server_index);
int remove_client(struct room* room, char* client_name, int server_index);
int insert_message(struct room* room, struct message* message);
struct message* find_message(struct room *room, int timestamp, int server_index);
int get_num_likes(struct message *message);
int add_like(struct message *message, char* username, bool liked, int counter, int server_index);
void get_messages(char* to_send, struct room* room);
void get_participants(char* to_send, struct room* room);
int clear_log(struct log **logs_ref, struct log **last_log_ref, int counter);
void insert_log(struct log **updates_ref, struct log *log);

#endif