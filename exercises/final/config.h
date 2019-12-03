#ifndef CONFIG_H
#define CONFIG_H

#define PORT "10030"
#define MAX_MESS_LEN 102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100
#define CONNECTION_TIMEOUT_SEC 10
#define CONNECTION_TIMEOUT_USEC 0
#define FREQ_SAVE       10

#define CONNECT 0
#define JOIN 1
#define ROOMCHANGE 2
#define MESSAGES 3
#define PARTICIPANTS_ROOM 4
#define PARTICIPANTS_SERVER 5
#define MATRIX 6
#define UPDATE_CLIENT 7 
#define UPDATE_NORMAL 8 
#define APPEND 9
#define UPDATE_MERGE 10
#define LIKES 11

#endif