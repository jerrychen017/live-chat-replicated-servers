#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

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
static int expected_timestamp[5];

static int my_server_index;

static char username[80];
static int ugrad_index;
static char room_name[80];
static char client_name[MAX_GROUP_NAME];

static struct room *rooms;

static int matrix[5][5];
static int my_timestamp; 

static char log_file_names[5][30];
static FILE *log_fd[5];
static struct log *logs[5];
static struct log *last_log[5];
static struct log *buffer; 
static struct log *end_of_buffer;

static char state_file_name[30];
//static FILE *state_fd;

static void Read_message();
static void Bye();
static int save_update(int timestamp, int server_index, char* update);
static int execute_append(int timestamp, int server_index, char *update);
static int execute_like(char *update);
static int execute_unlike(char *update);

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

    for (int i = 0; i < 5; i++) {
        logs[i] = NULL;
    }
    for (int i = 0; i < 5; i++) {
        last_log[i] = NULL;
    }
    buffer = NULL; 
    end_of_buffer = NULL; 
    my_timestamp = 0;

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

    // Initialize log file names and state file name
    for (int i = 0; i < 5; i++) {
        sprintf(log_file_names[i], "server%d-log%d.out", my_server_index, i + 1);
    }
    sprintf(state_file_name, "server%d-state.out", my_server_index);

    // If state file exists
    if (access(state_file_name, F_OK) != -1 ) {
        printf("Server: Reconstruct data structures from state file\n");
        // TODO: Reconstruct data structures from state file; retrieve 5 lamport timestamps
    }

    for (int i = 0; i < 5; i++) {
        // If log file exists
        if (access(log_file_names[i], F_OK) != -1 ) {
            /*
            TODO:
            read from the line matching with the corresponding timestamp
            save logs in memory
            */
        }
    }

    /* TODO:    
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
                printf("Server: send to client %s latest 25 messages of %s\nMESSAGES %s\n", sender, room_name, to_send);
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
                        if (old_room->participants[my_server_index - 1] != NULL) {
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
                        if (new_room->participants[my_server_index - 1] != NULL) {
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
                for (i = 0; i < 5; i++) {
                    // skip line with my server index
                    if (i + 1 == my_server_index) {
                        continue;
                    }
                    for (j = 0; j < 5; j++) {
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

                    // Reconcile on logs of 5 servers
                    for (j = 0; j < 5; j++) {

                        // Clear logs[server_index] list up to the lowest timestamp of all 5 servers
                        int lowest_timestamp = matrix[0][j];
                        for (i = 0; i < 5; i++) {
                            if (matrix[i][j] < lowest_timestamp) {
                                lowest_timestamp = matrix[i][j];
                            }
                        }
                        ret = clear_log(&logs[j], &last_log[j], lowest_timestamp);
                        if (ret < 0) {
                            printf("Error: fail to clear logs[%d] up to timestamp %d\n", j, lowest_timestamp);
                            break;
                        }
                        printf("Server: clear logs of server%d up to timestamp %d\n", j + 1, lowest_timestamp);

                        // Get lowest and highest timestamp of all ACTIVE servers for this server
                        if (!connected_servers[my_server_index - 1]) {
                            printf("Error: this server%d is not in the current network component\n", my_server_index);
                            break;
                        }
                        lowest_timestamp = matrix[my_server_index - 1][j];
                        int highest_timestamp = matrix[my_server_index - 1][j];
                        int server_index = my_server_index;
                        for (i = 0; i < 5; i++) {
                            if (connected_servers[i]) {
                                if (matrix[i][j] > highest_timestamp) {
                                    highest_timestamp = matrix[i][j];
                                    server_index = i + 1;
                                }
                                if (matrix[i][j] < lowest_timestamp) {
                                    lowest_timestamp = matrix[i][j];
                                }
                            } 
                        }
                        expected_timestamp[j] = highest_timestamp;

                        /* Find the server which has the highest timestamp, and lowest server index
                            to send missing updates for this server */
                        for (i = 0; i < 5; i++) {
                            if (connected_servers[i]) {
                                if (matrix[i][j] == highest_timestamp && i + 1 < server_index) {
                                    server_index = i + 1;
                                }
                            }
                        }

                        if (server_index != my_server_index) {
                            continue;
                        }

                        if (highest_timestamp == 0) {
                            continue;
                        }

                        // Current server is responsible for sending missing updates for server j + 1
                        // Send “UPDATE_MERGE <timestamp> <server_index> <update>” to servers group with logs from lowest to highest timestamp
                        struct log* cur = logs[j];
                        while (cur != NULL) {
                            if (cur->timestamp > lowest_timestamp) {
                                sprintf(to_send, "%d %d %s", cur->timestamp, cur->server_index, cur->content);
                                ret = SP_multicast(Mbox, AGREED_MESS, servers_group, UPDATE_MERGE, strlen(to_send), to_send);
                                if (ret < 0) {
				                    SP_error( ret );
				                    Bye();
			                    }
                                printf("Server: send log for reconcilation: timestamp %d server_index %d content %s\n", cur->timestamp, cur->server_index, cur->content);
                            }
                            cur = cur->next;
                        }
                    }

                    // If the current server has all logs up to date
                    bool merging_completed = true;
                    for (j = 0; j < 5; j++) {
                        if (matrix[my_server_index - 1][j] != expected_timestamp[j]) {
                            merging_completed = false;
                        }
                    }
                    if (merging_completed) {

                        printf("Server: matrix = \n");
                        for (i = 0; i < 5; i++) {
                            printf("\t");
                            for (int j = 0; j < 5; j++) {
                                printf("%d ", matrix[i][j]);
                            }
                            printf("\n");
                        }

                        // Mark as out of merging state
                        merging = false;
                        printf("Server: Finish merging\n");

                        // Execute updates received in buffer during merging
                        while (buffer != NULL) {

                            printf("Server: Execute update in buffer: timestamp %d server_index %d content %s\n", buffer->timestamp, buffer->server_index, buffer->content);

                            ret = save_update(buffer->timestamp, buffer->server_index, buffer->content);
                            if (ret < 0) {
                                printf("Error: fail to save update in buffer: %d %d %s\n", buffer->timestamp, buffer->server_index, buffer->content);
                            }

                            if (buffer->content[0] == 'a') {
                                ret = execute_append(buffer->timestamp, buffer->server_index, buffer->content);
                            } else if (buffer->content[0] == 'l') {
                                ret = execute_like(buffer->content);
                            } else if (buffer->content[0] == 'r') {
                                ret = execute_unlike(buffer->content);
                            } else {
                                printf("Error: unknown command in update in buffer: %d %d %s\n", buffer->timestamp, buffer->server_index, buffer->content);
                                break;
                            }
                            if (ret < 0) {
                                printf("Error: fail to execute update in buffer: %d %d %s\n", buffer->timestamp, buffer->server_index, buffer->content);
                                break;
                            }

                            struct log *to_delete = buffer; 
                            buffer = buffer->next; 
                            free(to_delete);
                        }
                        buffer = NULL; 
                        end_of_buffer = NULL;
                    }
                }

                break;
            }

            case UPDATE_CLIENT: 
            {
                // message = <update>

                // increment lamport timestamp
                my_timestamp++;

                // Send “UPDATE_NORMAL <timestamp> <my_server_index> <update>” to servers group
                char update[300];
                strcpy(update, message);
                sprintf(to_send, "%d %d %s", my_timestamp, my_server_index, update);
                ret = SP_multicast(Mbox, AGREED_MESS, servers_group, UPDATE_NORMAL, strlen(to_send), to_send);
                if (ret < 0) {
                    SP_error(ret);
                    Bye();
                }

                break; 
            }

            case UPDATE_NORMAL: 
            {
                printf("Receive UPDATE_NORMAL %s\n", message);
                // message = <timestamp> <server_index> <update>

                int timestamp;
                int server_index;
                char update[300];
                int num_read;

                ret = sscanf(message, "%d %d%n", &timestamp, &server_index, &num_read);
                if (ret < 2) {
                    printf("Error: cannot parse timestamp and server_index from UPDATE_NORMAL %s\n", message);
                    break;
                }

                strcpy(update, &message[num_read + 1]);

                // If in the middle of merging, put the update in buffer
                if (merging) { 
                    struct log *new_log = malloc(sizeof(struct log));
                    new_log->timestamp = timestamp;
                    new_log->server_index = server_index;
                    strcpy(new_log->content, update);
                    new_log->next = NULL;

                    if (buffer == NULL) {
                        buffer = new_log;
                        end_of_buffer = new_log;
                    } else {
                        end_of_buffer->next = new_log;
                        end_of_buffer = new_log;
                    }
                    break; 
                }

                // Save the update to file, memory and update timestamp & matrix accordingly
                ret = save_update(timestamp, server_index, update);
                if (ret < 0) {
                    printf("Error: fail to save update from UPDATE_NORMAL %s\n", message);
                    break;
                }

                // Execute the update on data structures, and send messages to clients
                if (update[0] == 'a') {
                    ret = execute_append(timestamp, server_index, update);
                } else if (update[0] == 'l') {
                    ret = execute_like(update);
                } else if (update[0] == 'r') {
                    ret = execute_unlike(update);
                } else {
                    printf("Error: unknown command in UPDATE_NORMAL %s\n", message);
                    break;
                }
                if (ret < 0) {
                    printf("Error: fail to execute update in UPDATE_NORMAL %s\n", message);
                    break;
                }

                /* TODO:
                For every FREQ_SAVE UPDATE_NORMAL messages received
                    Save state to state file
                */

                break;
            }

            case UPDATE_MERGE:
            {
                // message = <timestamp> <server_index> <update>

                // If the current server has finished merging
                if (!merging) {
                    break;
                }

                printf("Receive UPDATE_MERGE %s\n", message);

                int timestamp;
                int server_index;
                char update[300];
                int num_read;

                ret = sscanf(message, "%d %d%n", &timestamp, &server_index, &num_read);
                if (ret < 2) {
                    printf("Error: cannot parse timestamp and server_index from UPDATE_MERGE %s\n", message);
                    break;
                }

                strcpy(update, &message[num_read + 1]);
                
                // If update does not exist
                bool exist = true;
                if (timestamp > matrix[my_server_index - 1][server_index - 1]) {
                    exist = false;
                    ret = save_update(timestamp, server_index, update);
                    if (ret < 0) {
                        printf("Error: fail to save update from UPDATE_MERGE %s\n", message);
                        break;
                    }
                }

                if (update[0] == 'a') {
                    // If it is a new append update, i.e does not exist before
                    if (!exist) {
                        ret = execute_append(timestamp, server_index, update);
                    }

                // TODO: if the merging update is 'l' or 'r', update like_updates list
                } else if (update[0] == 'l') {
                    
                } else if (update[0] == 'r') {

                } else {
                    printf("Error: unknown command in UPDATE_MERGE %s\n", message);
                    break;
                }

                // If the current server has all logs up to date
                bool merging_completed = true;
                for (j = 0; j < 5; j++) {
                    if (matrix[my_server_index - 1][j] != expected_timestamp[j]) {
                        merging_completed = false;
                    }
                }
                if (merging_completed) {

                    printf("Server: matrix = \n");
                    for (i = 0; i < 5; i++) {
                        printf("\t");
                        for (int j = 0; j < 5; j++) {
                            printf("%d ", matrix[i][j]);
                        }
                        printf("\n");
                    }

                    /* TODO:
                    For every update in like_updates list
                        execute_like/execute_unlike
                    */

                    // Mark as out of merging state
                    merging = false;
                    printf("Server: Finish merging\n");

                    // Execute updates received in buffer during merging
                    while (buffer != NULL) {

                        printf("Server: Execute update in buffer: timestamp %d server_index %d content %s\n", buffer->timestamp, buffer->server_index, buffer->content);
                        
                        ret = save_update(buffer->timestamp, buffer->server_index, buffer->content);
                        if (ret < 0) {
                            printf("Error: fail to save update in buffer: %d %d %s\n", buffer->timestamp, buffer->server_index, buffer->content);
                        }

                        if (buffer->content[0] == 'a') {
                            ret = execute_append(buffer->timestamp, buffer->server_index, buffer->content);
                        } else if (buffer->content[0] == 'l') {
                            ret = execute_like(buffer->content);
                        } else if (buffer->content[0] == 'r') {
                            ret = execute_unlike(buffer->content);
                        } else {
                            printf("Error: unknown command in update in buffer: %d %d %s\n", buffer->timestamp, buffer->server_index, buffer->content);
                            break;
                        }
                        if (ret < 0) {
                            printf("Error: fail to execute update in buffer: %d %d %s\n", buffer->timestamp, buffer->server_index, buffer->content);
                            break;
                        }

                        struct log *to_delete = buffer; 
                        buffer = buffer->next; 
                        free(to_delete);
                    }
                    buffer = NULL; 
                    end_of_buffer = NULL;
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
                for (i = 0; i < 5; i++) {
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
                    for (i = 0; i < 5; i++) {
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
                for (i = 0; i < 5; i++) {
                    for (j = 0;j < 5; j++) {
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
                for (i = 0; i < 5; i++) {
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

static int save_update(int timestamp, int server_index, char* update)
{
    int ret;

    // Write update to “server[my_server_index]-log[server_index].out” file
    log_fd[server_index - 1] = fopen(log_file_names[server_index - 1], "a+");
    ret = fprintf(log_fd[server_index - 1], "%d %d %s\n", timestamp, server_index, update);
    if (ret < 0) {
        printf("Error: fail to write update %s to file %s\n", update, log_file_names[server_index - 1]);
        return -1;
    }
    fclose(log_fd[server_index - 1]);

    // Append it in logs[server_index] list
    struct log* new_log = malloc(sizeof(struct log));
    new_log->timestamp = timestamp;
    new_log->server_index = server_index;
    strcpy(new_log->content, update);
    new_log->next = NULL;

    if (logs[server_index - 1] == NULL) {
        logs[server_index - 1] = new_log;
        last_log[server_index - 1] = new_log;
    } else {
        last_log[server_index - 1]->next = new_log;
        last_log[server_index - 1] = new_log;
    }

    // Adopt the lamport timestamp if it is higher
    if (timestamp > my_timestamp) {
        my_timestamp = timestamp;
    }

    // Update matrix[my_server_index][server_index] to the new timestamp
    matrix[my_server_index - 1][server_index - 1] = timestamp;
    return 0;
}

static int execute_append(int timestamp, int server_index, char *update)
{
    int ret;
    char to_send[MAX_MESS_LEN];
    int num_read;

    // update = a <room_name> <username> <content>
    ret = sscanf(update, "a %s %s%n", room_name, username, &num_read);
    if (ret < 2) {
        printf("Error: cannot parse room_name and username from UPDATE_NORMAL %s\n", update);
    }

    // Insert message to messages list of the room, in the order of timestamp+server_index
    struct message *new_message = malloc(sizeof(struct message));
    new_message->timestamp = timestamp;
    new_message->server_index = server_index;
    strcpy(new_message->content, &update[num_read + 1]);
    strcpy(new_message->creator, username);
    new_message->liked_by = NULL;
    new_message->next = NULL;

    struct room* room = find_room(rooms, room_name);
    ret = insert_message(room, new_message);
    if (ret < 0) {
        printf("Error: fail to append message. %s does not exist\n", room_name);
        return -1;
    }

    printf("Server: append message created by %s to %s: %s\n", new_message->creator, room_name, new_message->content);

    if (room->participants[my_server_index - 1] != NULL) {
        // Send “APPEND <timestamp> <server_index> <username> <content>” to the server-room group
        sprintf(to_send, "%d %d %s %s", new_message->timestamp, new_message->server_index, new_message->creator, new_message->content);
        sprintf(server_room_group, "server%d-%s", my_server_index, room_name);
        ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, APPEND, strlen(to_send), to_send);
        if (ret < 0) {
            SP_error(ret);
            Bye();
        }
    }
    return 0;
}

static int execute_like(char *update)
{
    int ret;
    int timestamp;
    int server_index;
    char to_send[MAX_MESS_LEN];

    // update = l <room_name> <timestamp of the liked message> <server_index of the liked message> <username>
    ret = sscanf(update, "l %s %d %d %s", room_name, &timestamp, &server_index, username);
    if (ret < 4) {
        printf("Error: cannot parse room_name, timestamp, server_index and username from update %s\n", update);
        return -1;
    }

    struct room *room = find_room(rooms, room_name);
    if (room == NULL) {
        printf("Error: cannot find room named %s\n", room_name);
        return -1;
    }

    struct message *message = find_message(room, timestamp, server_index);
    if (message == NULL) {
        printf("Error: cannot find message with timestamp %d and server_index %d in room %s\n", timestamp, server_index, room_name);
        return -1;
    }

    // Check if user is the creator
    if (strcmp(username, message->creator) == 0) {
        printf("Server: message is created by %s and cannot be liked by the creator\n", username);
        return 0;
    }

    // Check if the message has been liked by the user
    int num_likes = add_like(message, username);
    if (num_likes < 0) {
        printf("Server: message has been liked by %s\n", username);
        return 0;
    }

    printf("Server: add like of %s to message %s in %s\n", username, message->content, room_name);

    // If the current server has participants in this room
    if (room->participants[my_server_index - 1] != NULL) {
        // Send “LIKES <message’s timestamp> <message’s server_index> <num_likes>” to the server-room group
        sprintf(to_send, "%d %d %d", timestamp, server_index, num_likes);
        sprintf(server_room_group, "server%d-%s", my_server_index, room_name);
        ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, LIKES, strlen(to_send), to_send);
        if (ret < 0) {
            SP_error(ret);
            Bye();
        }
    }

    return 0;
}

static int execute_unlike(char *update)
{
    int ret;
    int timestamp;
    int server_index;
    char to_send[MAX_MESS_LEN];

    // update = r <room_name> <timestamp of the liked message> <server_index of the liked message> <username>
    ret = sscanf(update, "r %s %d %d %s", room_name, &timestamp, &server_index, username);
    if (ret < 4) {
        printf("Error: cannot parse room_name, timestamp, server_index and username from update %s\n", update);
        return -1;
    }

    struct room *room = find_room(rooms, room_name);
    if (room == NULL) {
        printf("Error: cannot find room named %s\n", room_name);
        return -1;
    }

    struct message *message = find_message(room, timestamp, server_index);
    if (message == NULL) {
        printf("Error: cannot find message with timestamp %d and server_index %d in room %s\n", timestamp, server_index, room_name);
        return -1;
    }

    // Check if user is the creator
    if (strcmp(username, message->creator) == 0) {
        printf("Server: message is created by %s and cannot be unliked by the creator\n", username);
        return 0;
    }

    // Check if the message has been liked by the user
    int num_likes = remove_like(message, username);
    if (num_likes < 0) {
        printf("Server: message has not been liked by %s\n", username);
        return 0;
    }

    printf("Server: remove like of %s to message %s in %s\n", username, message->content, room_name);
    
    // If the current server has participants in this room
    if (room->participants[my_server_index - 1] != NULL) {
        // Send “LIKES <message’s timestamp> <message’s server_index> <num_likes>” to the server-room group
        sprintf(to_send, "%d %d %d", timestamp, server_index, num_likes);
        sprintf(server_room_group, "server%d-%s", my_server_index, room_name);
        ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, LIKES, strlen(to_send), to_send);
        if (ret < 0) {
            SP_error(ret);
            Bye();
        }
    }

    return 0;
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

int insert_message(struct room* room, struct message* message)
{
    if (room == NULL) {
        return -1;
    }
    if (room->messages == NULL) {
        room->messages = message;
        return 0;
    }

    struct message dummy;
    dummy.next = room->messages;
    struct message *cur = &dummy;
    while (cur->next != NULL) {
        if ((cur->next->timestamp > message->timestamp) 
            || (cur->next->timestamp == message->timestamp && cur->next->server_index > message->server_index)) {
            
            // insert at current position
            message->next = cur->next;
            cur->next = message;
            room->messages = dummy.next;

            return 0;
        }
        cur = cur->next;
    }

    // insert at last position
    message->next = cur->next;
    cur->next = message;
    room->messages = dummy.next;

    return 0;
}

struct message* find_message(struct room *room, int timestamp, int server_index)
{
    if (room == NULL || room->messages == NULL) {
        return NULL;
    }

    struct message *cur = room->messages;
    while (cur != NULL) {
        if (cur->timestamp == timestamp && cur->server_index == server_index) {
            return cur;
        }
        cur = cur->next;
    }

    return NULL;
}

int add_like(struct message *message, char* username)
{
    if (message == NULL) {
        return -2;
    }

    if (message->liked_by == NULL) {
        struct participant *new_participant = malloc(sizeof(struct participant));
        strcpy(new_participant->name, username);
        new_participant->next = NULL;

        message->liked_by = new_participant;
        return 1;
    }

    struct participant dummy;
    dummy.next = message->liked_by;

    struct participant *cur = &dummy;

    int num_likes = 0;

    while (cur->next != NULL) {
        // if username already exists in liked_by list
        if (strcmp(cur->next->name, username) == 0) {
            return -1;
        }
        cur = cur->next;
        num_likes++;
    }

    struct participant *new_participant = malloc(sizeof(struct participant));
    strcpy(new_participant->name, username);
    new_participant->next = NULL;

    cur->next = new_participant;
    num_likes++;

    return num_likes;
}

int remove_like(struct message *message, char *username)
{
    if (message == NULL) {
        return -2;
    }

    struct participant dummy;
    dummy.next = message->liked_by;

    struct participant *cur = &dummy;

    int num_likes = 0;
    bool deleted = false;

    while (cur->next != NULL) {
        if (strcmp(cur->next->name, username) == 0) {
            // remove participant at cur->next
            struct participant *to_delete = cur->next;
            cur->next = cur->next->next;
            free(to_delete);
            deleted = true;
        }
        cur = cur->next;
        num_likes++;
    }

    // user does not exist in the list
    if (!deleted) {
        return -1;
    }

    return num_likes;
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

int clear_log(struct log **logs_ref, struct log **last_log_ref, int timestamp)
{
    if (timestamp == 0) {
        return 0;
    }

    if (*logs_ref == NULL || *last_log_ref == NULL) {
        return -1;
    }

    struct log *cur = *logs_ref;
    while (cur != NULL) {
        if (cur->timestamp <= timestamp) {
            // delete log at current position
            struct log *to_delete = cur;
            cur = cur->next;
            free(to_delete);
        } else {
            break;
        }
    }

    *logs_ref = cur;
    if (cur == NULL) {
        *last_log_ref = NULL;
    }

    return 0;
}