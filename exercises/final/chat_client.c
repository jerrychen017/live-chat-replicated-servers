/*
<server_index> is in the range [1,5]
we assume each server program runs with a unique <server_index> 

MEMBRESHIPS: 
* this server's public group
* 'servers' group
*/
#include "message.h"
#include "sp.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>

static char User[80];
static char Spread_name[80];
static char Private_group[MAX_GROUP_NAME];
static char public_group[80];
static const char servers_group[80] = "servers";
static mailbox Mbox;

static int server_index;

int main(int argc, char *argv[])
{
    return 0;
}