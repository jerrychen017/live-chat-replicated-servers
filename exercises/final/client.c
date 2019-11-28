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

static struct participant* participants;
static struct participant* last_participant;
static struct message* messages;
static struct message* last_message;
static int num_messages;

static	void	Print_menu();
static	void	User_command();
static	void	Read_message();
static  void    Display();
static  void	Bye();

int main(int argc, char *argv[])
{
    logged_in = false;
    connected = false;
    joined = false;
    server_group[0] = '\0';
    server_client_group[0] = '\0';

    participants = NULL;
    messages = NULL;
    last_message = NULL;

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
    char    content[81]; // message sent by the client using 'a'
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

            // username cannot contain hashtag or hyphen
            for (i = 0; i < strlen(username); i++) {
                if (username[i] == '#' || username[i] == '-') {
                    printf(" invalid username, cannot contain hashtag or hyphen \n");
				    break;
                }
            }

            // Leave server-room groups if exist
            if (joined) {
                ret = SP_leave(Mbox, server_room_group);
                if (ret < 0) {
                    SP_error( ret );
                }
                printf("Client: leave room %s\n", room_name);
                joined = false;
            }

            // Leave previous server-client group if exists
            if (connected) {
                ret = SP_leave( Mbox, server_client_group );
			    if (ret < 0) {
                    SP_error( ret );
                }
                printf("Client: disconnect from server %d\n", server_index);
                connected = false;
            }

            if (logged_in) {
                SP_disconnect(Mbox);
                logged_in = false;
                E_detach_fd(Mbox, READ_FD);
            }

            // clear participants list
            while (participants != NULL) {
                struct participant* to_delete = participants;
                participants = participants->next;
                free(to_delete);
            }
            // clear messages list
            while (messages != NULL) {
                struct message* message_to_delete = messages;
                messages = messages->next;
                free(message_to_delete);
            }
            num_messages = 0;

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

            // Leave server-room groups if exist
            if (joined) {
                ret = SP_leave(Mbox, server_room_group);
                if (ret < 0) {
                    SP_error( ret );
                }
                printf("Client: leave room %s\n", room_name);
                joined = false;
            }

            // Leave previous server-client group if exists
            if (connected) {
                ret = SP_leave( Mbox, server_client_group );
			    if (ret < 0) {
                    SP_error( ret );
                }
                printf("Client: disconnect from server %d\n", server_index);
                connected = false;
            }

            // clear participants list
            while (participants != NULL) {
                struct participant* to_delete = participants;
                participants = participants->next;
                free(to_delete);
            }
            // clear messages list
            while (messages != NULL) {
                struct message* message_to_delete = messages;
                messages = messages->next;
                free(message_to_delete);
            }
            num_messages = 0;

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

            // start timer and check if server responds and connects
            sp_time connection_timeout; 
            connection_timeout.sec = CONNECTION_TIMEOUT_SEC;
            connection_timeout.usec = CONNECTION_TIMEOUT_USEC; 
            E_queue(connection_timeout_event, server_index, NULL, connection_timeout);

            break;
        }

        case 'j':
        {
            ret = sscanf( &command[2], "%s", room_name );
            if (ret < 1) {
				printf(" invalid room name \n");
				break;
			}

            // room_name cannot contain hashtag or hyphen
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

        case 'a': 
        {

            if (!logged_in) {
                printf("Client: did not login using command 'u'\n");
                break;
            }

            if (!connected) {
                printf("Client: did not connect with server using command 'c'\n");
                break;
            }

            if (!joined) {
                printf("Client: did not join a room using command 'j'\n");
                break; 
            }

            if (strlen(&command[2]) == 0 || (strlen(&command[2]) == 1 && content[2] == '\n')) {
                printf("Client: message cannot be empty");
                break;
            }

            if (strlen(&command[2]) > 81) {
                printf("Client: maximum number of characters in a line is 80\n");
                break;
            }

            strcpy(content, &command[2]);

            // Remove new line from message
            if (content[strlen(content)-1] == '\n') {
                content[strlen(content)-1] = '\0';
            }

            // Send “UPDATE_CLIENT a <room_name> <username> <content>” to the server’s public group
            sprintf(message, "a %s %s %s", room_name, username, content);
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, UPDATE_CLIENT, strlen(message), message);
            if (ret < 0) {
                SP_error(ret);
            }

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
                printf("Receive MESSAGES %s\n", message);
                // clear messages list
                while (messages != NULL) {
                    struct message* to_delete = messages;
                    messages = messages->next;
                    free(to_delete);
                }
                num_messages = 0;

                char temp[200];
                temp[0] = '\0';
                int timestamp;
                int message_server_index;
                char creator[80];
                int num_likes;
                char content[80];
                for (i = 0; i < strlen(message); i++) {
                    temp[strlen(temp) + 1] = '\0';
                    temp[strlen(temp)] = message[i];

                    if (message[i] == '\n') {
                        // append a message
                        // Each message: <timestamp> <server_index> <creator> <num_likes> <content>
                        ret = sscanf(temp, "%d %d %s %d %[^\n]\n", &timestamp, &message_server_index, creator, &num_likes, content);
                        if (ret < 5) {
                            printf("Error: cannot parse message from MESSAAGES line %s\n", message);
                            continue;
                        }
                        struct message* new_message = malloc(sizeof(struct message));
                        new_message->timestamp = timestamp;
                        new_message->server_index = message_server_index;
                        strcpy(new_message->creator, creator);
                        new_message->num_likes = num_likes;
                        strcpy(new_message->content, content);
                        new_message->next = NULL;

                        if (messages == NULL) {
                            messages = new_message;
                            last_message = new_message;
                        } else {
                            last_message->next = new_message;
                            last_message = new_message;
                        }

                        num_messages++;

                        temp[0] = '\0';
                    }
                }

                Display();

                break;
            }

            case PARTICIPANTS_ROOM:
            {
                // message = <user1> <user2> ...

                // clear participants list
                while (participants != NULL) {
                    struct participant* to_delete = participants;
                    participants = participants->next;
                    free(to_delete);
                }

                char client_name[80];
                client_name[0] = '\0';
                for (i = 0; i < strlen(message); i++) {
                    if (message[i] != ' ') {
                        client_name[strlen(client_name) + 1] = '\0';
                        client_name[strlen(client_name)] = message[i];
                    } else {

                        // append a participant
                        struct participant* new_participant = malloc(sizeof(struct participant));
                        strcpy(new_participant->name, client_name);
                        new_participant->next = NULL;

                        if (participants == NULL) {
                            participants = new_participant;
                            last_participant = new_participant;
                        } else {
                            last_participant->next = new_participant;
                            last_participant = new_participant;
                        }

                        client_name[0] = '\0';
                    }
                }

                Display();

                break;
            }

            case APPEND:
            {
                printf("Receive APPEND %s\n", message);
                // message = <timestamp> <server_index> <username> <content>

                int timestamp;
                int message_server_index;
                char creator[80];
                int num_read;
                ret = sscanf(message, "%d %d %s%n", &timestamp, &message_server_index, creator, &num_read);
                if (ret < 3) {
                    printf("Error: cannot parse timestamp, server_index and username from APPEND %s\n", message);
                    break;
                }

                struct message* new_message = malloc(sizeof(struct message));
                new_message->timestamp = timestamp;
                new_message->server_index = message_server_index;
                strcpy(new_message->creator, creator);
                strcpy(new_message->content, &message[num_read + 1]);
                new_message->num_likes = 0;
                new_message->next = NULL;

                if (num_messages == 25) {
                    // Remove first message
                    struct message *to_delete = messages;
                    messages = to_delete->next;
                    free(to_delete);
                    num_messages--;
                }

                // Append new message to list
                if (messages == NULL) {
                    messages = new_message;
                    last_message = new_message;
                } else {
                    last_message->next = new_message;
                    last_message = new_message;
                }
                num_messages++;

                Display();

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
                        E_dequeue(connection_timeout_event, server_index, NULL); 
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
                    
                    // Leave server-room group if exists
                    if (joined) {
                        ret = SP_leave(Mbox, server_room_group);
                        if (ret < 0) {
                            SP_error( ret );
                        }
                        printf("Client: leave room %s\n", room_name);
                        joined = false;
                    }

                    // clear participants list
                    while (participants != NULL) {
                        struct participant* to_delete = participants;
                        participants = participants->next;
                        free(to_delete);
                    }
                    // clear messages list
                    while (messages != NULL) {
                        struct message* message_to_delete = messages;
                        messages = messages->next;
                        free(message_to_delete);
                    }
                    num_messages = 0;
                    
                    ret = SP_leave( Mbox, server_client_group );
			        if (ret < 0) {
                        SP_error( ret );
                    }
                    printf("Client: server%d disconnects; please reconnect with another server\n", server_index);
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

static void Display() {

    // Do not display if participants is empty. Waiting for server's message
    if (participants == NULL) {
        return;
    }

    printf("Room: %s\n", room_name);
    printf("Current participants: ");
    struct participant* cur = participants;
    while (cur != NULL) {
        printf("%s ", cur->name);
        cur = cur->next;
    }
    printf("\n");

    int line = 1;
    struct message* message_cur = messages;
    while (message_cur != NULL) {
        printf("%d. %s: %-*sLikes: %d\n",
            line, message_cur->creator, 40, message_cur->content, message_cur->num_likes);
        message_cur = message_cur->next;
        line++;
    }
}

static void Bye()
{
	To_exit = 1;
	printf("\nBye.\n");

    // Leave server-room groups if exist
    if (joined) {
        SP_leave(Mbox, server_room_group);
    }    
    if (connected) {
        SP_leave( Mbox, server_client_group );
    }
	if (logged_in) {
        SP_disconnect(Mbox);
    }
	exit(0);
}

void connection_timeout_event(int index, void * data) { 
     // Leave current server-room group
    int ret = SP_leave(Mbox, server_client_group);
    if (ret < 0) {
        SP_error( ret );
    }
    printf("Client: server%d did not respond after timeout\n", index);
}