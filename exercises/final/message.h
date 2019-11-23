#ifndef MESSAGE_H
#define MESSAGE_H

#define CLIENT_C 0
#define CLIENT_J 1
#define CLIENT_A 2
#define CLIENT_L 3
#define CLIENT_R 4
#define CLIENT_H 5
#define SERVER_JOIN_ROOM 6
#define SERVER_A 7
#define SERVER_L 8
#define SERVER_R 9
#define SERVER_ROOM_CHANGE 10

struct message
{
    unsigned int type;
    unsigned int server_index;
    /* 
    client message: indicate server which the message is being sent to
    server message: indicate the server that sent the message
    */
    unsigned int timestamp;
    /**/
    char[20] creator;  // <username> of the creator
    char[100] message; // TODO: change message limit
    char[100] liked_users;
};

#endif