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

static FILE *state_fd;
static FILE *log1_fd;
static FILE *log2_fd;
static FILE *log3_fd;
static FILE *log4_fd;
static FILE *log5_fd;

static int server_index;
static int matrix[5][5];

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("chat_server usage: chatserver <server_index>.\n");
        exit(1);
    }
    server_index = atoi(argv[1]);
    if (server_index < 1 || server_index > 5)
    {
        perror("chat_server error: invalid <server_index>.\n");
    }

    sprintf(User, "server%d", server_index);
    sprintf(public_group, "server%d", server_index);
    sprintf(Spread_name, "4803");

    int ret;
    int mver, miver, pver;
    sp_time test_timeout;

    test_timeout.sec = 5;
    test_timeout.usec = 0;

    if (!SP_version(&mver, &miver, &pver))
    {
        printf("main: Illegal variables passed to SP_version()\n");
        exit(1);
    }
    ret = SP_connect_timeout(Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout);
    if (ret != ACCEPT_SESSION)
    {
        SP_error(ret);
        exit(1);
    }

    // joining this server's public group
    ret = SP_join(Mbox, public_group);
    if (ret < 0)
    {
        SP_error(ret);
        exit(1);
    }

    // joining servers group
    ret = SP_join(Mbox, servers_group);
    if (ret < 0)
    {
        SP_error(ret);
        exit(1);
    }

    // TODO: initialize matrix

    // open state and log files in appending and reading mode
    char state_filename[20];
    sprintf(state_filename, "server%d-state.out", server_index);
    fd = fopen(state_filename, "a+");
    char log1_filename[20];
    sprintf(log1_filename, "server%d-log1.out", server_index);
    fd = fopen(log1_filename, "a+");
    char log2_filename[20];
    sprintf(log2_filename, "server%d-log2.out", server_index);
    fd = fopen(log2_filename, "a+");
    char log3_filename[20];
    sprintf(log3_filename, "server%d-log3.out", server_index);
    fd = fopen(log3_filename, "a+");
    char log4_filename[20];
    sprintf(log4_filename, "server%d-log4.out", server_index);
    fd = fopen(log4_filename, "a+");
    char log5_filename[20];
    sprintf(log5_filename, "server%d-log5.out", server_index);
    fd = fopen(log5_filename, "a+");

    E_init();
    E_attach_fd(Mbox, READ_FD, receive_messages, 0, NULL, HIGH_PRIORITY);
    E_handle_events();
    return (0);
}

static void receive_messages()
{
    struct message mess;
    char sender[MAX_GROUP_NAME];
    char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
    membership_info memb_info;
    int num_groups;
    int service_type;
    int16 mess_type;
    int endian_mismatch;
    int i;
    int ret;

    service_type = 0;
    ret = SP_receive(Mbox, &service_type, sender, 100, &num_groups, target_groups,
                     &mess_type, &endian_mismatch, sizeof(struct message), (char *)&mess);

    if (ret < 0)
    {
        if ((ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT))
        {
            service_type = DROP_RECV;
            printf("\n========Buffers or Groups too Short=======\n");
            ret = SP_receive(Mbox, &service_type, sender, 100, &num_groups, target_groups,
                             &mess_type, &endian_mismatch, sizeof(struct message), (char *)&mess);
        }
    }

    struct message packet;
    if (Is_regular_mess(service_type))
    { /* receive message packet*/
        if (Is_agreed_mess(service_type))
        {
            switch (mess_type)
            {
            case CLIENT_C:
            {
                break;
            }
            case CLIENT_J:
            {
                break;
            }
            }
        }
        else
        {
            printf("Warning: didn't receive AGREED \n");
        }
    }
    else if (Is_membership_mess(service_type))
    { /* receive membership packet*/
        ret = SP_get_memb_info((char *)&mess, service_type, &memb_info);
        if (ret < 0)
        {
            printf("BUG: membership message does not have valid body\n");
            SP_error(ret);
            exit(1);
        }



        if (Is_reg_memb_mess(service_type))
        {
            printf("Received REGULAR membership for group %s with %d members, where I am member %d:\n",
                   sender, num_groups, mess_type);
            
            if( Is_caused_join_mess( service_type ) )
			{
				printf("Due to the JOIN of %s\n", memb_info.changed_member );
			}else if( Is_caused_leave_mess( service_type ) ){
				printf("Due to the LEAVE of %s\n", memb_info.changed_member );
			}else if( Is_caused_disconnect_mess( service_type ) ){
				printf("Due to the DISCONNECT of %s\n", memb_info.changed_member );
			}else if( Is_caused_network_mess( service_type ) ){
				printf("Due to NETWORK change with %u VS sets\n", memb_info.num_vs_sets);
		    }
	    }else if( Is_transition_mess(   service_type ) ) {
			printf("received TRANSITIONAL membership for group %s\n", sender );
		}else if( Is_caused_leave_mess( service_type ) ){
			printf("received membership message that left group %s\n", sender );
		} else {printf("received incorrecty membership message of type 0x%x\n", service_type );}
        
    } else if ( Is_reject_mess( service_type ) ) {
		printf("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n",
			sender, service_type, mess_type, endian_mismatch, num_groups, ret, mess );
    }
	
    } else { 
        printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret); 
    }
}