#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "config.h"
#include "client.h"

static char Spread_name[80] = PORT;
static mailbox Mbox;
static char Private_group[MAX_GROUP_NAME];
static int To_exit = 0;

static char username[80];
static int ugrad_index;
static bool logged_in;
static int server_index;
static bool connected;
static char room_name[80];
static bool joined;

static char server_group[80];
static char server_client_group[80 + 15];
static char server_room_group[80 + 8];

static	void	Print_menu();
static	void	User_command();
static	void	Read_message();
static  void	Bye();

int main(int argc, char *argv[])
{
    logged_in = false;
    connected = false;
    joined = false;
    server_group[0] = '\0';
    server_client_group[0] = '\0';

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
	char	message[MAX_MESS_LEN];
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

            // username cannot contain hashtag and space
            for (i = 0; i < strlen(username); i++) {
                if (username[i] == '#' || username[i] == '-') {
                    printf(" invalid username, cannot contain hashtag, space \n");
				    break;
                }
            }

            // TODO: Leave server-room groups if exist

            // Leave previous server-client group if exists
            if (connected) {
                ret = SP_leave( Mbox, server_client_group );
			    if (ret < 0) {
                    SP_error( ret );
                }
                connected = false;
            }

            if (logged_in) {
                SP_disconnect(Mbox);
                logged_in = false;
                E_detach_fd(Mbox, READ_FD);
            }

            // TODO: Clear data structures, like messages, participants, room_name

            sp_time test_timeout;
            test_timeout.sec = 5;
            test_timeout.usec = 0;

            // Start client's private group
            ret = SP_connect_timeout( Spread_name, username, 0, 1, &Mbox, Private_group, test_timeout );
            if (ret != ACCEPT_SESSION) {
		        SP_error( ret );
		        break;
            }
            logged_in = true;
            ret = sscanf(Private_group, "#%*[^#]#ugrad%d", &ugrad_index);
            if (ret < 1) {
                printf("Error: cannot parse ugrad index from private group name\n");
                break;
            }
            E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );
            
            printf("Client: start private group %s\n", Private_group );

            break;
        }

        case 'c':
        {
            if (!logged_in) {
                printf("Client: did not login using command 'u'\n");
                break;
            }

            ret = sscanf( &command[2], "%d", &server_index );
            if (ret < 1) {
				printf(" invalid server index [1-5] \n");
				break;
			}
            if (server_index < 1 || server_index > 5) {
                printf(" invalid server index [1-5] \n");
				break;
            }

            // TODO: Leave server-room group if exists

            // Leave previous server-client group if exists
            if (connected) {
                ret = SP_leave( Mbox, server_client_group );
			    if (ret < 0) {
                    SP_error( ret );
                }
                connected = false;
            }

            // TODO: Clear data structures, like messages, participants

            // Join new server-client group
            ret = sprintf( server_client_group, "server%d-%s-ugrad%d", server_index, username, ugrad_index );
            if (ret < 3) {
                printf("Error: cannot construct server-client group name\n");
                break;
            }
            ret = SP_join( Mbox, server_client_group );
			if (ret < 0) {
                SP_error( ret );
            }

            // Send CONNECT message to the server
            ret = sprintf(server_group, "server%d", server_index);
            if (ret < 1) {
                printf("Error: cannot construct name of server's public group\n");
                break;
            }
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, CONNECT, 0, message);

            // TODO: start timer and check if server responds and connects

            break;
        }

        case 'j':
        {
            ret = sscanf( &command[2], "%s", room_name );
            if (ret < 1) {
				printf(" invalid room name \n");
				break;
			}

            // room_name cannot contain hashtag and hyphen
            for (i = 0; i < strlen(room_name); i++) {
                if (room_name[i] == '#' || room_name[i] == '-') {
                    printf(" invalid room name, cannot contain hashtag or hyphen \n");
				    break;
                }
            }

            if (strcmp(room_name, "null") == 0) {
                printf(" invalid room name, cannot be null\n");
                break;
            }

            if (!logged_in) {
                printf("Client: did not login using command 'u'\n");
                break;
            }

            if (!connected) {
                printf("Client: did not connect with server using command 'c'\n");
                break;
            }

            // Leave previous server-room group, if exists
            if (joined) {
                printf("Client: leave previous room\n");
                ret = SP_leave(Mbox, server_room_group);
                if (ret < 0) {
                    SP_error( ret );
                }
                joined = false;
            }

            // Join new server-room group
            sprintf(server_room_group, "server%d-%s", server_index, room_name);
            ret = SP_join(Mbox, server_room_group);
            if (ret < 0) {
                SP_error( ret );
            }
            joined = true;

            // Send “JOIN <room_name>” message to the server’s public group
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, JOIN, strlen(room_name), room_name);
            printf("request to join room %s\n", room_name);

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

    if (ret < 0) {
	    if (!To_exit) {
            // Spread daemon crashes
			SP_error( ret );
            printf("Client: Spread daemon crashes; disconnect from server and log out\n");
            E_detach_fd(Mbox, READ_FD);
            logged_in = false;
            connected = false;
		}
	}

    if (Is_regular_mess(service_type)) {

        message[ret] = 0;

        if (!Is_agreed_mess(service_type)) {
            printf("Warning: did not received AGREED message\n");
            return;
        }

        switch (mess_type) {
            case MESSAGES:
            {
                printf("Client: Receive MESSAGES %s\n", message);
                break;
            }

            case PARTICIPANTS_ROOM:
            {
                printf("Client: Receive PARTICIPANTS_ROOM %s\n", message);
                break;
            }

            default:
            {
                printf("Warning: receive unknown message type\n");
            }
        }

    } else if (Is_membership_mess(service_type)) {
        
        ret = SP_get_memb_info( message, service_type, &memb_info );
        
        if (ret < 0) {
            printf("BUG: membership message does not have valid body\n");
            SP_error( ret );
            exit(1);
        }

        if (Is_reg_memb_mess(service_type)) {
            printf("Received REGULAR membership for group %s with %d members, where I am member %d:\n",
				sender, num_groups, mess_type );
            for( i=0; i < num_groups; i++ ) {
                printf("\t%s\n", &target_groups[i][0] );
            }

            // ** server-client group **
            if (strcmp(sender, server_client_group) == 0) {
                if (Is_caused_join_mess(service_type)) {
                    
                    if (strcmp(memb_info.changed_member, Private_group) == 0) {
                        // client itself joins server-client group
                    } else {
                        // server joins server-client group
                        int temp_server_index = 0;
                        ret = sscanf(memb_info.changed_member, "#server%d#ugrad%*d", &temp_server_index);
                        if (ret < 1 || temp_server_index != server_index) {
                            printf("Error: cannot parse server index from server name\n");
                        } else {
                            printf("Client: successfully connected to server%d\n", server_index);
                            connected = true;
                        }
                    }
                    
                } else if (Is_caused_disconnect_mess(service_type)
                            || Is_caused_network_mess(service_type)) {
                    
                    // TODO: Leave server-room group if exists
                    // TODO: Clear data structures, like messages, participants
                    
                    printf("Client: server%d disconnects; please reconnect with another server\n", server_index);
                    ret = SP_leave( Mbox, server_client_group );
			        if (ret < 0) {
                        SP_error( ret );
                    }
                    connected = false;
                } else if (Is_caused_leave_mess(service_type)) {
                    printf("Warning: should not receive LEAVE message in %s\n", server_client_group);
                }
            }

            printf("\n============================\n");

		    if (Is_caused_join_mess(service_type)) {
			    printf("Due to the JOIN of %s\n", memb_info.changed_member );
		    } else if (Is_caused_leave_mess(service_type)) {
			    printf("Due to the LEAVE of %s\n", memb_info.changed_member );
		    } else if (Is_caused_disconnect_mess(service_type)) {
			    printf("Due to the DISCONNECT of %s\n", memb_info.changed_member );
		    } else if (Is_caused_network_mess(service_type)) {
			    printf("Due to NETWORK change with %u VS sets\n", memb_info.num_vs_sets);
                num_vs_sets = SP_get_vs_sets_info( message, &vssets[0], MAX_VSSETS, &my_vsset_index );
                if (num_vs_sets < 0) {
                    printf("BUG: membership message has more then %d vs sets. Recompile with larger MAX_VSSETS\n", MAX_VSSETS);
                    SP_error( num_vs_sets );
                    exit( 1 );
                }
                for (i = 0; i < num_vs_sets; i++) {
                    printf("%s VS set %d has %u members:\n",
                        (i  == my_vsset_index) ?
                        ("LOCAL") : ("OTHER"), i, vssets[i].num_members );
                    ret = SP_get_vs_set_members(message, &vssets[i], members, MAX_MEMBERS);
                    if (ret < 0) {
                        printf("VS Set has more then %d members. Recompile with larger MAX_MEMBERS\n", MAX_MEMBERS);
                        SP_error( ret );
                        exit( 1 );
                    }
                    for (j = 0; j < vssets[i].num_members; j++) {
                        printf("\t%s\n", members[j] );
                    }
			    }
		    } 
        } else if (Is_caused_leave_mess(service_type)) {

			printf("Client: leave group %s\n", sender);
	        
        } else if (Is_transition_mess(service_type)) {
			printf("received TRANSITIONAL membership for group %s\n", sender );
		} else {
            printf("received incorrecty membership message of type 0x%x\n", service_type );
        }
    } else {
        printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);
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
    if (connected) {
        SP_leave( Mbox, server_client_group );
    }
	if (logged_in) {
        SP_disconnect(Mbox);
    }
	exit(0);
}