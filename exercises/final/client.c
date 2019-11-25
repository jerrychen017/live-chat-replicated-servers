#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "message.h"

static char Spread_name[80] = PORT;
static mailbox Mbox;
static char Private_group[MAX_GROUP_NAME];
static int Num_sent;
static unsigned int Previous_len;
static int To_exit = 0;

static char username[80];
static bool loggedIn;
static int server_index;
static bool connected;

static	void	Print_menu();
static	void	User_command();
static	void	Read_message();
static  void	Bye();

int main(int argc, char *argv[])
{
    loggedIn = false;
    connected = false;

    E_init();
    E_attach_fd( 0, READ_FD, User_command, 0, NULL, LOW_PRIORITY );

    Print_menu();
    printf("\nClient> ");
    fflush(stdout);
    
	E_handle_events();

    return 0;
}

static	void	Print_menu()
{
	printf("\n");
	printf("============\n");
	printf("Client Menu:\n");
	printf("------------\n");
	printf("\n");
	printf("\tu <username> -- login with a username\n");
	printf("\tc <server_index> -- connect with a server [1-5]\n");
	printf("\n");
	printf("\tj <room_name> -- join a chat room\n");
	printf("\ta <content> -- append a message to the chat\n");
	printf("\tl <line_number> -- mark line as liked\n");
	printf("\tr <line_number> -- remove like from a line\n");
	printf("\th -- print history of the chat room\n");
	printf("\tv -- print membership of the servers\n");
	fflush(stdout);
}

static void User_command()
{
    char	command[130];
	char	mess[MAX_MESS_LEN];
	char	group[80];
	char	groups[10][MAX_GROUP_NAME];
	int	num_groups;
	unsigned int mess_len;
	int	ret;
	int	i;

    for (i=0; i < sizeof(command); i++) {
        command[i] = 0;
    }
    if (fgets( command, 130, stdin ) == NULL) {
        Bye();
    }

    switch(command[0])
    {
        case 'u':
        {
            ret = sscanf( &command[2], "%s", username );
            if (ret < 1) {
				printf(" invalid username \n");
				break;
			}

            // TODO: Leave server-client, server-room groups if exist
            if (loggedIn) {
                SP_disconnect(Mbox);
                loggedIn = false;
                E_detach_fd(Mbox, READ_FD);
            }

            // TODO: Clear data structures, like messages, participants, room_name

            sp_time test_timeout;
            test_timeout.sec = 5;
            test_timeout.usec = 0;

            // start client's private group
            ret = SP_connect_timeout( Spread_name, username, 0, 1, &Mbox, Private_group, test_timeout );
            printf("here\n");
            if (ret != ACCEPT_SESSION) {
		        SP_error( ret );
		        break;
            }
            loggedIn = true;
            E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );
            printf("Client: start private group %s\n", Private_group );

            break;
        }

        case 'c':
        {
            break;
        }

        default:
        {
            printf("\nUnknown commnad\n");
			Print_menu();
			break;
        }
    }

    printf("\nClient> ");
	fflush(stdout);

}

static void Read_message()
{
    static char	message[MAX_MESS_LEN];
    char	 sender[MAX_GROUP_NAME];
    char	 target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
    membership_info  memb_info;
    vs_set_info      vssets[MAX_VSSETS];
    unsigned int     my_vsset_index;
    int      num_vs_sets;
    char     members[MAX_MEMBERS][MAX_GROUP_NAME];
    int		 num_groups;
    int		 service_type;
    int16	 mess_type;
    int		 endian_mismatch;
    int		 i,j;
    int		 ret;

    service_type = 0;

    ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
        &mess_type, &endian_mismatch, sizeof(message), message );
	printf("\n============================\n");

    /* if( ret < 0 ) {
        if ( (ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT) ) {
            service_type = DROP_RECV;
            printf("\n========Buffers or Groups too Short=======\n");
            ret = SP_receive( Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups, 
                &mess_type, &endian_mismatch, sizeof(message), message );
        }
    }*/
    if (ret < 0) {
	    if (!To_exit) {
            // Spread daemon crashes
			SP_error( ret );
			printf("\n============================\n");
			printf("\nBye.\n");
		}
		exit(0);
	}

    printf("\n");
	printf("Client> ");
	fflush(stdout);


}

static void Bye()
{
	To_exit = 1;
	printf("\nBye.\n");

    // TODO: Leave server-client, server-room groups if exist

    // conditional disconnection
	if (loggedIn) {
        SP_disconnect(Mbox);
    }
	exit(0);
}