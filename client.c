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
static int line_number; 

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

            if (strlen(username) > 10) {
                printf(" invalid username, cannot be longer than 10 characters \n");
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

            if (strlen(room_name) > 23) {
                printf(" invalid room name, cannot be longer than 23 characters \n");
                break;
            }

            // room_name cannot contain hashtag or hyphen
            for (i = 0; i < strlen(room_name); i++) {
                if (room_name[i] == '#' || room_name[i] == '-') {
                    printf(" invalid room name, cannot contain hashtag or hyphen \n");
				    break;
                }
            }

            // room_name cannot be null
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

            // clear participants list
            while (participants != NULL) {
                struct participant* to_delete = participants;
                participants = participants->next;
                free(to_delete);
            }

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

            if (strlen(&command[2]) == 0 || (strlen(&command[2]) == 1 && command[2] == '\n')) {
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

            bool allSpace = true;
            for (int i = 0; i < strlen(content); i++) {
                if (content[i] != ' ') {
                    allSpace = false;
                    break;
                }
            }
            if (allSpace) {
                printf("Client: message cannot be all whitespace\n");
                break;
            }

            // Send “UPDATE_CLIENT a <room_name> <username> <content>” to the server’s public group
            sprintf(message, "a %s %s %s", room_name, username, content);
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, UPDATE_CLIENT, strlen(message), message);
            if (ret < 0) {
                SP_error(ret);
            }

            break;
        }

        case 'l':
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

            ret = sscanf(&command[2], "%d", &line_number);
            if (ret < 1) {
				printf(" invalid line number [1-25]\n");
				break;
			}
            if (line_number < 1 || line_number > num_messages) {
                printf(" invalid line number [1-25] \n");
				break;
            }

            // Find the message’s counter and server index in the messages list
            struct message *cur_mess = messages;
            for (int i = 0; i < line_number - 1; i++) {
                cur_mess = cur_mess->next;
            }
            int liked_timestamp = cur_mess->counter; 
            int liked_server_index = cur_mess->server_index;

            // Send “UPDATE_CLIENT l <room_name> <counter of the liked message> <server_index of the liked message> <username>” to the server’s public group
            sprintf(message, "l %s %d %d %s", room_name, liked_timestamp, liked_server_index, username);
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, UPDATE_CLIENT, strlen(message), message);
            if (ret < 0) {
                SP_error(ret);
                Bye();
            }

            break; 
        }

        case 'r':
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

            ret = sscanf(&command[2], "%d", &line_number);
            if (ret < 1) {
				printf(" invalid line number [1-25]\n");
				break;
			}
            if (line_number < 1 || line_number > num_messages) {
                printf(" invalid line number [1-25] \n");
				break;
            }

            // Find the message’s counter and server index in the messages list
            struct message *cur_mess = messages;
            for (int i = 0; i < line_number - 1; i++) {
                cur_mess = cur_mess->next;
            }
            int unliked_timestamp = cur_mess->counter; 
            int unliked_server_index = cur_mess->server_index;

            // Send “UPDATE_CLIENT r <room_name> <counter of the liked message> <server_index of the liked message> <username>” to the server’s public group
            sprintf(message, "r %s %d %d %s", room_name, unliked_timestamp, unliked_server_index, username);
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, UPDATE_CLIENT, strlen(message), message);
            if (ret < 0) {
                SP_error(ret);
                Bye();
            }

            break;
        }

        case 'v':
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
            
            message[0] = '\0';
            // Send "VIEW" to the server’s public group
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, VIEW, strlen(message), message);
            if (ret < 0) {
                SP_error(ret);
                Bye();
            }
            break;
        }

        case 'h':
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

            message[0] = '\0';
            // Send "HISTORY <room_name>" to the server’s public group
            sprintf(message, "%s", room_name);
            ret = SP_multicast(Mbox, AGREED_MESS, server_group, HISTORY, strlen(message), message);
            if (ret < 0) {
                SP_error(ret);
                Bye();
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
    int		 num_groups;
    int		 service_type;
    int16	 mess_type;
    int		 endian_mismatch;
    int		 i;
    int		 ret;

    service_type = 0;

    ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
        &mess_type, &endian_mismatch, sizeof(message), message );

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
                //printf("Receive MESSAGES %s\n", message);
                // clear messages list
                while (messages != NULL) {
                    struct message* to_delete = messages;
                    messages = messages->next;
                    free(to_delete);
                }
                num_messages = 0;

                char temp[200];
                temp[0] = '\0';
                int counter;
                int message_server_index;
                char creator[80];
                int num_likes;
                char content[80];
                for (i = 0; i < strlen(message); i++) {
                    temp[strlen(temp) + 1] = '\0';
                    temp[strlen(temp)] = message[i];

                    if (message[i] == '\n') {
                        // append a message
                        // Each message: <counter> <server_index> <creator> <num_likes> <content>
                        ret = sscanf(temp, "%d %d %s %d %[^\n]\n", &counter, &message_server_index, creator, &num_likes, content);
                        if (ret < 5) {
                            printf("Error: cannot parse message from MESSAAGES line %s\n", message);
                            continue;
                        }
                        struct message* new_message = malloc(sizeof(struct message));
                        new_message->counter = counter;
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
                // printf("Receive APPEND %s\n", message);
                // message = <counter> <server_index> <username> <content>

                int counter;
                int message_server_index;
                char creator[80];
                int num_read;
                ret = sscanf(message, "%d %d %s%n", &counter, &message_server_index, creator, &num_read);
                if (ret < 3) {
                    printf("Error: cannot parse counter, server_index and username from APPEND %s\n", message);
                    break;
                }

                struct message* new_message = malloc(sizeof(struct message));
                new_message->counter = counter;
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

            case LIKES:
            {
                // message = <message’s counter> <message’s server_index> <num_likes>
                int counter;
                int message_server_index;
                int num_likes;
                ret = sscanf(message, "%d %d %d", &counter, &message_server_index, &num_likes);
                if (ret < 3) {
                    printf("Error: cannot parse counter, server_index and num_likes from LIKES %s\n", message);
                    break;
                }

                // Find the message in list
                struct message *cur = messages;
                while (cur != NULL) {
                    if (cur->counter == counter && cur->server_index == message_server_index) {
                        // Update num_likes for the message
                        cur->num_likes = num_likes;
                        Display();
                        break;
                    }
                    cur = cur->next;
                }

                break;
            }

            case VIEW:
            {
                int connected_servers[5];
                ret = sscanf(message, "%d %d %d %d %d", &connected_servers[0], &connected_servers[1],
                    &connected_servers[2], &connected_servers[3], &connected_servers[4]);
                if (ret < 5) {
                    printf("Error: did not receive 5 numbers in VIEW %s\n", message);
                    break;
                }

                printf("Client: current network has\n");
                for (int i = 0; i < 5; i++) {
                    if (connected_servers[i] == 1) {
                        printf("\tserver%d\n", i + 1);
                    }
                }

                break;
            }

            case HISTORY:
            {
                // message = <creator> <num_likes> <content>
                char creator[80];
                int num_likes;
                int num_read;

                ret = sscanf(message, "%s %d%n", creator, &num_likes, &num_read);
                if (ret < 2) {
                    printf("Error: cannot parse creator and num_likes from HISTORY %s\n", message);
                    break;
                }

                if (num_likes != 0) {
                    printf("%s: %-*sLikes: %d\n", creator, 40, &message[num_read + 1], num_likes);
                } else {
                    printf("%s: %s\n", creator, &message[num_read + 1]);
                }

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

    printf("\nRoom: %s\n", room_name);
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
        if (message_cur->num_likes != 0) {
            printf("%d. %s: %-*sLikes: %d\n",
                line, message_cur->creator, 40, message_cur->content, message_cur->num_likes);
        } else {
            printf("%d. %s: %s\n",
                line, message_cur->creator, message_cur->content);
        }
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