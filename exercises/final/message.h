#ifndef MESSAGE_H
#define MESSAGE_H

#define PORT "10100"
#define MAX_MESS_LEN 102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100

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
    char creator[20];  // <username> of the creator
    char message[100]; // TODO: change message limit
    char liked_users[100];
};

#endif