#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "config.h"
#include "server.h"

static char User[80];
static char Spread_name[80] = PORT;
static char Private_group[MAX_GROUP_NAME];
static mailbox Mbox;
static int To_exit = 0;

static char public_group[80];
static const char servers_group[80] = "servers";
static char server_room_group[80 + 8];

static bool merging;
static bool connected_servers[5];
static int num_matrices;

static int my_server_index;

static char username[80];
static int ugrad_index;
static char room_name[80];
static char client_name[MAX_GROUP_NAME];

static struct room *rooms;

static int matrix[5][5];
/*static FILE *state_fd;
static FILE *log1_fd;
static FILE *log2_fd;
static FILE *log3_fd;
static FILE *log4_fd;
static FILE *log5_fd;*/


static void Read_message();
static void Bye();

// TODO: make sure one server index is used exactly once

int main(int argc, char *argv[])
{
    rooms = NULL;
    merging = false;
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            matrix[i][j] = 0;
        }
    }
    num_matrices = 0;

    int	ret;

    if (argc != 2) {
        printf("server usage: chatserver <server_index>.\n");
        exit(1);
    }
    ret = sscanf(argv[1], "%d", &my_server_index);
    if (ret < 1) {
        printf("server usage: invalid <server_index> [1-5].\n");
        exit(1);
    }
    if (my_server_index < 1 || my_server_index > 5) {
        printf("server usage: invalid <server_index> [1-5].\n");
        exit(1);
    }

    sprintf(User, "server%d", my_server_index);
    sprintf(public_group, "server%d", my_server_index);

    sp_time test_timeout;
    test_timeout.sec = 5;
    test_timeout.usec = 0;

    ret = SP_connect_timeout(Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout);
    if (ret != ACCEPT_SESSION) {
        SP_error(ret);
        Bye();
    }

    // joining the server's public group
    ret = SP_join(Mbox, public_group);
    if (ret < 0) {
        SP_error(ret);
        exit(1);
    }
    printf("Server: join public group %s\n", public_group);

    // joining servers group
    ret = SP_join(Mbox, servers_group);
    if (ret < 0) {
        SP_error(ret);
        exit(1);
    }
    printf("Server: join group %s\n", servers_group);

    // open state and log files in appending and reading mode
    /*char state_filename[20];
    sprintf(state_filename, "server%d-state.out", my_server_index);
    state_fd = fopen(state_filename, "a+");
    char log1_filename[20];
    sprintf(log1_filename, "server%d-log1.out", my_server_index);
    log1_fd = fopen(log1_filename, "a+");
    char log2_filename[20];
    sprintf(log2_filename, "server%d-log2.out", my_server_index);
    log2_fd = fopen(log2_filename, "a+");
    char log3_filename[20];
    sprintf(log3_filename, "server%d-log3.out", my_server_index);
    log3_fd = fopen(log3_filename, "a+");
    char log4_filename[20];
    sprintf(log4_filename, "server%d-log4.out", my_server_index);
    log4_fd = fopen(log4_filename, "a+");
    char log5_filename[20];
    sprintf(log5_filename, "server%d-log5.out", my_server_index);
    log5_fd = fopen(log5_filename, "a+");*/

    /* TODO:
    if there is state file
        Reconstruct data structures from state file; retrieve 5 lamport timestamps
    else
        initialize empty data structures, timestamp = 0 

    for every server
        if log file exists
            read from the line matching with the corresponding timestamp
            save logs in memory
        else
            initialize empty logs[server_index] list
    
    Execute logs in the order of lamport timestamp + process_index
        update rooms, matrix and timestamp accordingly
    
    */
    
    E_init();
    E_attach_fd(Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY);
    E_handle_events();
    return (0);
}

static void Read_message()
{
    static char	message[MAX_MESS_LEN];
    static char to_send[MAX_MESS_LEN];
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
			printf("\n============================\n");
			printf("\nBye.\n");
		}
        exit(0);
	}

    if (Is_regular_mess(service_type)) {

        message[ret] = 0;

        if (!Is_agreed_mess(service_type)) {
            printf("Warning: did not received AGREED message\n");
            return;
        }

        switch (mess_type) {

            case CONNECT:
            {
                char server_client_group[80 + 15];
                ret = sscanf(sender, "#%[^#]#ugrad%d", username, &ugrad_index);
                if (ret < 2) {
                    printf("Error: cannot parse username and ugrad index from client's name\n");
                    break;
                }
                ret = sprintf( server_client_group, "server%d-%s-ugrad%d", my_server_index, username, ugrad_index );
                if (ret < 3) {
                    printf("Error: cannot construct server-client group name\n");
                    break;
                }
                printf("Server: client %s requests connection\n", sender);
                ret = SP_join( Mbox, server_client_group );
			    if (ret < 0) {
                    SP_error( ret );
                }
                break;
            }

            case JOIN:
            {
                // sender = client's private group
                // message = <room_name>
                sscanf(message, "%s", room_name);

                // Search rooms and see if the client is previously in any room
                struct room* old_room = find_room_of_client(rooms, sender, my_server_index);
                char old_room_name[80];
                if (old_room == NULL) {
                    strcpy(old_room_name, "null");
                } else {
                    strcpy(old_room_name, old_room->name);
                }

                // Send ROOMCHANGE <client_name> <old_room> <new_room> <my_server_index> to servers group
                sprintf(to_send, "%s %s %s %d", sender, old_room_name, room_name, my_server_index);
                ret = SP_multicast(Mbox, AGREED_MESS, servers_group, ROOMCHANGE, strlen(to_send), to_send);
                if (ret < 0) {
				    SP_error( ret );
				    Bye();
			    }

                printf("Server: client %s requests to switch from %s to %s\n", sender, old_room_name, room_name);

                // Send up to latest 25 messages of this room to the client’s private group
                struct room* new_room = find_room(rooms, room_name);
                get_messages(to_send, new_room);
                ret = SP_multicast(Mbox, AGREED_MESS, sender, MESSAGES, strlen(to_send), to_send);
                if (ret < 0) {
				    SP_error( ret );
				    Bye();
			    }
                break;
            }

            case ROOMCHANGE:
            {
                // message = <client_name> <old_room> <new_room> <server_index>
                char old_room_name[80];
                char new_room_name[80];
                int server_index;
                sscanf(message, "%s %s %s %d", client_name, old_room_name, new_room_name, &server_index);

                printf("Receive ROOMCHANGE %s\n", message);

                if (strcmp(old_room_name, "null") != 0) {
                    // Remove client from participants[server_index] in the old room
                    struct room* old_room = find_room(rooms, old_room_name);
                    if (old_room == NULL) {
                        printf("Error: old %s does not exist", old_room_name);
                    }
                    ret = remove_client(old_room, client_name, server_index);
                    if (ret < 0) {
                        printf("Error: fail to remove client %s from %s", client_name, old_room_name);
                    } else {
                        printf("Server: remove client %s from %s\n", client_name, old_room_name);
                        // Send new participant list to the server-room group
                        get_participants(to_send, old_room);
                        sprintf(server_room_group, "server%d-%s", my_server_index, old_room_name);
                        ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, PARTICIPANTS_ROOM, strlen(to_send), to_send);
                        if (ret < 0) {
				            SP_error( ret );
				            Bye();
			            }
                    }
                }

                if (strcmp(new_room_name, "null") != 0) {
                    // Add client to participants[server_index] in the new room
                    struct room* new_room = find_room(rooms, new_room_name);
                    if (new_room == NULL) {
                        new_room = create_room(&rooms, new_room_name);
                    }
                    ret = add_client(new_room, client_name, server_index);
                    if (ret < 0) {
                        printf("Error: fail to add client %s to %s", client_name, new_room_name);
                    } else {
                        printf("Server: add client %s to %s\n", client_name, new_room_name);
                        // Send new participant list to the server-room group
                        get_participants(to_send, new_room);
                        sprintf(server_room_group, "server%d-%s", my_server_index, new_room_name);
                        ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, PARTICIPANTS_ROOM, strlen(to_send), to_send);
                        if (ret < 0) {
				            SP_error( ret );
				            Bye();
			            }
                    }
                }

                break;
            }

            case PARTICIPANTS_SERVER:
            {
                // message = <room_name> <server_index> <client1> <client2> \n...
                int server_index;
                int num_read;
                ret = sscanf(message, "%s %d%n", room_name, &server_index, &num_read);
                if (ret < 2) {
                    printf("Error: cannot parse room name and server index from PARTICIPANTS_SERVER %s\n", message);
                    break;
                }
                
                // Ignore the message sent by the server itself
                if (server_index == my_server_index) {
                    break;
                }

                printf("Server: server%d has clients in %s\n", server_index, room_name);

                // Create the room if it does exist in the rooms list
                struct room* room = find_room(rooms, room_name);
                if (room == NULL) {
                    room = create_room(&rooms, room_name);
                }

                // Clear participants[server_index] list in this room
                clear_client(room, server_index);

                // Construct participants[server_index] list
                client_name[0] = '\0';
                for (i = num_read + 1; i < strlen(message); i++) {
                    if (message[i] != ' ') {
                        client_name[strlen(client_name) + 1] = '\0';
                        client_name[strlen(client_name)] = message[i];
                    } else {
                        ret = add_client(room, client_name, server_index);
                        if (ret < 0) {
                            printf("Error: fail to add client %s to room%s\n", client_name, room_name);
                        }
                        printf("\t%s\n", client_name);
                        client_name[0] = '\0';
                    }
                }

                break;
            }

            case MATRIX:
            {
                printf("Receive MATRIX from server %s\n", sender);
                // message = <25 integers>

                if (!merging) {
                    printf("Warning: receive MATRIX not in merging state\n");
                }
                
                num_matrices--;
                if (num_matrices < 0) {
                    printf("Error: number of matrices received is larger than expected\n");
                }

                // Parse 25 integers
                int received_matrix[5][5];
                char temp[10];
                temp[0] = '\0';
                int counter = 0;
                int temp_int;
                for (int i = 0; i < strlen(message); i++) {
                    if (message[i] != ' ') {
                        temp[strlen(temp) + 1] = '\0';
                        temp[strlen(temp)] = message[i];
                    } else {
                        ret = sscanf(temp, "%d", &temp_int);
                        if (ret < 1) {
                            printf("Error: cannot parse integer from %s\n in MATRIX %s\n", temp, message);
                        }
                        received_matrix[counter / 5][counter % 5] = temp_int;
                        counter++;
                        temp[0] = '\0';
                    }
                }
                if (counter != 25) {
                    printf("Error: did not receive 25 integers in MATRIX %s\n", message);
                }

                // Adopt all integers if it is higher, except for line matrix[my_server_index]
                for (int i = 0; i < 5; i++) {
                    // skip line with my server index
                    if (i + 1 == my_server_index) {
                        continue;
                    }
                    for (int j = 0; j < 5; j++) {
                        if (received_matrix[i][j] > matrix[i][j]) {
                            matrix[i][j] = received_matrix[i][j];
                        }
                    }
                }

                // If just received expected number of matrices
                if (num_matrices == 0) {
                    
                    // TODO: Clear liked_updates list

                    // Send new participant list to my server-room group, if I have clients in this room
                    struct room* cur = rooms;
                    while (cur != NULL) {
                        if (cur->participants[my_server_index - 1] != NULL) {
                            get_participants(to_send, cur);
                            sprintf(server_room_group, "server%d-%s", my_server_index, cur->name);
                            ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, PARTICIPANTS_ROOM, strlen(to_send), to_send);
                            if (ret < 0) {
				                SP_error( ret );
				                Bye();
			                }
                        }
                        cur = cur->next;
                    }

                    // TODO: reconcile on logs

                    // TODO: If there is no updates to merge
                    merging = false;
                    printf("Server: Finish merging\n");

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
            printf("Received REGULAR membership for group %s with %d members, where I am member %d:\n",
				sender, num_groups, mess_type );
            for( i=0; i < num_groups; i++ ) {
                printf("\t%s\n", &target_groups[i][0] );
            }

            // Membership change in server-client group
            if (sscanf(sender, "server%*d-%[^-]-ugrad%d", username, &ugrad_index) == 2) {
                if (Is_caused_join_mess(service_type)) {
                    // server itself joins server-client group
                    if (strcmp(memb_info.changed_member, Private_group) == 0) {
                        printf("Server: connect with client #%s#ugrad%d\n", username, ugrad_index);
                    }
                } else if (Is_caused_leave_mess(service_type)
                            || Is_caused_disconnect_mess(service_type)
                            || Is_caused_network_mess(service_type)) {

                    // client disconnects, or reconnects to another server
                    printf("Server: client #%s#ugrad%d disconnects\n", username, ugrad_index);
                    
                    // If the client is previously in a room
                    struct room* room = find_room_of_client(rooms, memb_info.changed_member, my_server_index);
                    if (room != NULL) {
                        // Send "ROOMCHANGE <client_name> <old_room> <null> <my_server_index>f" to servers group
                        sprintf(to_send, "%s %s null %d", memb_info.changed_member, room->name, my_server_index);
                        ret = SP_multicast(Mbox, AGREED_MESS, servers_group, ROOMCHANGE, strlen(to_send), to_send);
                        if (ret < 0) {
				            SP_error( ret );
				            Bye();
			            }
                    }

                    // Leave server-client group
                    ret = SP_leave( Mbox, sender );
                    if (ret < 0) {
                        SP_error( ret );
                    }
                }

            // Membership change in servers group
            } else if (strcmp(sender, "servers") == 0) {

                printf("Server: network changes. Start merging.\n");

                merging = true;

                
                // Find servers in the current network component
                for (int i = 0; i < 5; i++) {
                    connected_servers[i] = false;
                }

                printf("Server: current network component has\n");

                int server_index;
                for( i=0; i < num_groups; i++ ) {
                    printf("\t%s\n", &target_groups[i][0]);
                    ret = sscanf(&target_groups[i][0], "#server%d#ugrad%*d", &server_index);
                    if (ret < 1) {
                        printf("Error: cannot parse server index from %s\n", &target_groups[i][0]);
                    } else {
                        connected_servers[server_index - 1] = true;
                    }
                }

                struct room* cur = rooms;
                while (cur != NULL) {
                    // Clear participants[server_index] for servers not in the current network component
                    for (int i = 0; i < 5; i++) {
                        if (!connected_servers[i]) {
                            clear_client(cur, i + 1);
                        }
                    }

                    // Send participants[my_server_index] to the servers group
                    struct participant* client_cur = cur->participants[my_server_index - 1];
                    char clients[MAX_MESS_LEN - 86];
                    clients[0] = '\0';
                    while (client_cur != NULL) {
                        strcat(clients, client_cur->name);
                        strcat(clients, " ");
                        client_cur = client_cur->next;
                    }

                    // Construct "PARTICIPANTS_SERVER <room_name> <server_index> <client1> <client2> ..."
                    sprintf(to_send, "%s %d %s", cur->name, my_server_index, clients);
                    ret = SP_multicast(Mbox, AGREED_MESS, servers_group, PARTICIPANTS_SERVER, strlen(to_send), to_send);
                    if (ret < 0) {
				        SP_error( ret );
				        Bye();
			        }
                    cur = cur->next;
                }

                // Send “MATRIX <25 integers>” to servers group
                to_send[0] = '\0';
                char temp[10];
                for (int i = 0; i < 5; i++) {
                    for (int j = 0;j < 5; j++) {
                        sprintf(temp, "%d", matrix[i][j]);
                        strcat(to_send, temp);
                        strcat(to_send, " ");
                    }
                }

                ret = SP_multicast(Mbox, AGREED_MESS, servers_group, MATRIX, strlen(to_send), to_send);
                if (ret < 0) {
			        SP_error( ret );
			        Bye();
		        }

                // Calculate how many matrices expected to receive
                
                num_matrices = 0;
                for (int i = 0; i < 5; i++) {
                    if (connected_servers[i]) {
                        num_matrices++;
                    }
                }
            }


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
			
            printf("Server: leave group %s\n", sender );

		} else if (Is_transition_mess(service_type)) {
			printf("received TRANSITIONAL membership for group %s\n", sender );
		} else {
            printf("received incorrecty membership message of type 0x%x\n", service_type );
        }
    } else {
        printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);
    }
}

static void Bye()
{
	To_exit = 1;

	printf("\nBye.\n");

    SP_disconnect(Mbox);
	exit(0);
}

/* Helper functions */

struct room* create_room(struct room** rooms_ref, char* room_name)
{
    struct room* new_room = malloc(sizeof(struct room));
    strcpy(new_room->name, room_name);
    for (int i = 0; i < 5; i++) {
        new_room->participants[i] = NULL;
    }
    new_room->messages = NULL;
    new_room->next = NULL;

    rooms = *rooms_ref;
    if (rooms == NULL) {
        *rooms_ref = new_room;
        return new_room;
    }

    while (rooms->next != NULL) {
        rooms = rooms->next;
    }

    rooms->next = new_room;
    return new_room;
}

struct room* find_room(struct room* rooms, char* room_name)
{
    while (rooms != NULL) {
        if (strcmp(rooms->name, room_name) == 0) {
            return rooms;
        }
        rooms = rooms->next;
    }

    return NULL;
}

struct room* find_room_of_client(struct room* rooms, char* client_name, int server_index)
{
    while (rooms != NULL) {
        struct participant* participants = rooms->participants[server_index - 1];
        if (find_client(participants, client_name)) {
            return rooms;
        }
        rooms = rooms->next;
    }
    return NULL;
}

bool find_client(struct participant* list, char* client_name)
{
    while (list != NULL) {
        if (strcmp(list->name, client_name) == 0) {
            return true;
        }
        list = list->next;
    }
    return false;
}

void clear_client(struct room* room, int server_index)
{
    struct participant* list = room->participants[server_index - 1];
    while (list != NULL) {
        struct participant* to_delete = list;
        list = list->next;
        free(to_delete);
    }
    room->participants[server_index - 1] = NULL;
}

int add_client(struct room* room, char* client_name, int server_index)
{
    if (room == NULL) {
        return -1;
    }

    struct participant* new_participant = malloc(sizeof(struct participant));
    strcpy(new_participant->name, client_name);
    new_participant->next = NULL;

    struct participant* participants = room->participants[server_index - 1];

    if (participants == NULL) {
        room->participants[server_index - 1] = new_participant;
        return 0;
    }

    while (participants->next != NULL) {
        participants = participants->next;
    }

    participants->next = new_participant;
    return 0;
}

int remove_client(struct room* room, char* client_name, int server_index)
{
    if (room == NULL) {
        return -1;
    }
    
    struct participant* participants = room->participants[server_index - 1];

    struct participant dummy;
    dummy.next = participants;

    struct participant* cur = &dummy;
    while (cur->next != NULL) {
        if (strcmp(cur->next->name, client_name) == 0) {
            struct participant* to_delete = cur->next;
            cur->next = cur->next->next;
            free(to_delete);
            room->participants[server_index - 1] = dummy.next;
            return 0;
        }
        cur = cur->next;
    }
    return -1;
}

void get_messages(char* to_send, struct room* room) {
    if (room == NULL || room->messages == NULL) {
        sprintf(to_send, "%d", 0);
        return;
    }

    struct message* end = room->messages;
    struct message* first = room->messages;
    int steps = 0;
    while (end != NULL && steps < 25) {
        end = end->next;
        steps++;
    }

    while (end != NULL) {
        first = first->next;
        end = end->next;
    }

    to_send[0] = '\0';
    char message[200];
    while (first != NULL) {
        int num_likes = 0;
        struct participant* participants = first->liked_by;
        while (participants != NULL) {
            num_likes++;
            participants = participants->next;
        }
        // Every message: <timestamp> <server_index> <creator> <num_likes> <content>\n
        sprintf(message, "%d %d %s %d %s\n", first->timestamp, first->server_index, first->creator, num_likes, first->content);
        strcat(to_send, message);
        first = first->next;
    }

}

void get_participants(char* to_send, struct room* room)
{
    if (room == NULL) {
        printf("Warning: room is NULL in get_participants() method\n");
        sprintf(to_send, "%d", 0);
        return;
    }
    
    struct participant* list = NULL;
    struct participant* last_node = NULL;
    
    for (int i = 0; i < 5; i++) {
        struct participant* participants = room->participants[i];
        while (participants != NULL) {
            int ret = sscanf(participants->name, "#%[^#]#ugrad%*d", username);
            if (ret < 1) {
                printf("Error: cannot parse username from client name %s\n", participants->name);
                participants = participants->next;
                continue;
            }

            // Check if the username is already in usernames list
            if (find_client(list, username)) {
                participants = participants->next;
                continue;
            }

            // add new username to list
            struct participant* new_username = malloc(sizeof(struct participant));
            strcpy(new_username->name, username);
            new_username->next = NULL;

            if (list == NULL) {
                list = new_username;
                last_node = new_username;
            } else {
                last_node->next = new_username;
                last_node = new_username;
            }
            participants = participants->next;
        }
    }

    // Construct "PARTICIPANTS_ROOM <user1> <user2> ..." message
    to_send[0] = '\0';
    struct participant* cur = list;
    while (cur != NULL) {
        strcat(to_send, cur->name);
        strcat(to_send, " ");
        cur = cur->next;
    }

    // delete the list
    while (list != NULL) {
        struct participant* to_delete = list;
        list = list->next;
        free(to_delete);
    }

}