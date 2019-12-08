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

static bool received_highest_counter[5];
static struct log* updates; 

static int my_server_index;

static char username[80];
static int ugrad_index;
static char room_name[80];
static char client_name[MAX_GROUP_NAME];

static struct room *rooms;

static int matrix[5][5];
static int my_counter;
static int my_index;
static int num_updates; // number of updates expected to receive during merging
static bool received_matrix[5]; // if I have received matrix during merging 
static bool received_start; // if received start merging signal, to prevent from receiving updates from previous network change
static int sent_updates[5];

static char log_file_names[5][30];

static FILE *log_fd[5];
static struct log *logs[5];
static struct log *last_log[5];
static struct log *buffer; 
static struct log *end_of_buffer;

static char state_file_name[30];
static FILE *state_fd;
static int num_update_normal; 

static void Read_message();
static void Bye();
static int read_state();
static int write_state();
static int read_log(int i);
static void clear();
static int save_update(int counter, int server_index, int index, char* update, bool write_to_file);
static int execute_append(int counter, int server_index, char *update);
static int execute_like(int counter, int server_index, char *update);
static int execute_unlike(int counter, int server_index, char *update);
static int execute_join(char *update);
static int execute_roomchange(char *update);
static int execute_history(char *update);
static int execute_buffer(struct log *log);

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
    updates = NULL;

    num_updates = 0;
    received_start = false;


    for (int i = 0; i < 5; i++) {
        logs[i] = NULL;
        last_log[i] = NULL;
        received_highest_counter[i] = false;
        received_matrix[5] = false;
    }

    buffer = NULL;
    end_of_buffer = NULL;
    my_counter = 0;
    my_index = 0;
    num_update_normal = 0;

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
        state_fd = fopen(state_file_name, "r");
        
        // Reconstruct data structures from state file
        ret = read_state(state_fd);
        if (ret < 0) {
            // fail to read from state file
            // clear data structures and set matrix, counter and index to 0
            clear();
        }

        ret = fclose(state_fd);
        if (ret < 0) {
            printf("Error: fail to close state file %s\n", state_file_name);
            exit(1);
        }
    }

    for (int i = 0; i < 5; i++) {

        // If log file exists
        if (access(log_file_names[i], F_OK) != -1 ) {

            printf("Server: read logs from log file %s\n", log_file_names[i]);
              
            log_fd[i] = fopen(log_file_names[i], "r");
            if(log_fd[i] == NULL) {
                printf("Error: cannot open log file %s\n", log_file_names[i]);
                continue;
            }

            // Read logs in file, and put in logs list or updates list
            ret = read_log(i);
            if (ret < 0) {
                printf("Error: fail to read log from file %s\n", log_file_names[i]);
                // clear log list
                while (logs[i] != NULL) {
                    struct log *to_delete = logs[i];
                    logs[i] = logs[i]->next;
                    free(to_delete);
                }
                logs[i] = NULL;
                last_log[i] = NULL;
            }

            ret = fclose(log_fd[i]);
            if (ret < 0) {
                printf("Error: fail to close log file %s\n", log_file_names[i]);
                continue;
            }
        }
    }

    // Execute logs in the order of counter + server_index
    while(updates != NULL) {

        printf("Server: execute update from log file: %d %d %d %s\n", updates->counter, updates->server_index, updates->index, updates->content);

        // TODO: move around node instead of free and reallocate
        ret = save_update(updates->counter, updates->server_index, updates->index, updates->content, false);
        if (ret < 0) {
            printf("Error: fail to save update read from file: %d %d %d %s\n", updates->counter, updates->server_index, updates->index, updates->content);
            continue;
        }

        // Adopt the index if the update is from myself
        if (updates->server_index == my_server_index) {
            my_index = updates->index;
        }
                        
        if (updates->content[0] == 'a') {
            // content = a <room_name> <username> <content>
            ret = sscanf(updates->content, "a %s", room_name);
            if (ret < 1) {
                printf("Error: fail to parse room name from update %s\n", updates->content);
            }

            // Create the room if does not exist
            struct room* room = find_room(rooms, room_name);
            if (room == NULL) {
                create_room(&rooms, room_name);
            }

            ret = execute_append(updates->counter, updates->server_index, updates->content);
        } else if(updates->content[0] == 'l') {
            ret = execute_like(updates->counter, updates->server_index, updates->content);
        } else if(updates->content[0] == 'r') {
            ret = execute_unlike(updates->counter, updates->server_index, updates->content);
        } else {
            printf("Error: unknown command %s in updates list\n", updates->content); 
        }
        if (ret < 0) {
            printf("Server: fail to execute update %s\n", updates->content);
        }

        struct log* to_delete = updates;
        updates = updates->next;
        free(to_delete);
    }    
    
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

                // Join server-client group
                ret = SP_join( Mbox, server_client_group );
			    if (ret < 0) {
                    SP_error(ret);
                }
                break;
            }

            case JOIN:
            {
                // sender = client's private group
                // message = <room_name>

                ret = sscanf(message, "%s", room_name);
                if (ret < 1) {
                    printf("Error: fail to parse room name from JOIN %s\n", message);
                    break;
                }

                if (merging) {
                    struct log *new_log = malloc(sizeof(struct log));
                    // content = j <room_name> <client_name>
                    sprintf(new_log->content, "j %s %s", room_name, sender);
                    new_log->next = NULL;

                    // Append to buffer list
                    if (buffer == NULL) {
                        buffer = new_log;
                        end_of_buffer = new_log;
                    } else {
                        end_of_buffer->next = new_log;
                        end_of_buffer = new_log;
                    }

                    break;
                }

                char update[300];
                // update = <room_name> <client_name>
                sprintf(update, "%s %s", room_name, sender);
                ret = execute_join(update);
                if (ret < 0) {
                    printf("Error: fail to execute JOIN %s\n", message);
                }

                break;
            }

            case ROOMCHANGE:
            {
                printf("Receive ROOMCHANGE %s\n", message);
                // message = <client_name> <old_room> <new_room> <server_index>

                if (merging) {
                    struct log *new_log = malloc(sizeof(struct log));
                    // content = R <client_name> <old_room> <new_room> <server_index>
                    sprintf(new_log->content, "R %s", message);
                    new_log->next = NULL;

                    // Append to buffer list
                    if (buffer == NULL) {
                        buffer = new_log;
                        end_of_buffer = new_log;
                    } else {
                        end_of_buffer->next = new_log;
                        end_of_buffer = new_log;
                    }

                    break;
                }

                ret = execute_roomchange(message);
                if (ret < 0) {
                    printf("Error: fail to execute ROOMCHANGE %s\n", message);
                }

                break;
            }

            case START:
            {
                int alive_servers[5];
                
                ret = sscanf(message, "%d %d %d %d %d", 
                    &alive_servers[0],
                    &alive_servers[1], 
                    &alive_servers[2], 
                    &alive_servers[3], 
                    &alive_servers[4]);
                if (ret < 5) {
                    printf("Error: cannot parse 5 integers from START %s\n", message);
                    break;
                }

                // Check if 5 integers match with connected_servers array
                bool same = true;
                for (int i = 0; i < 5; i++) {
                    if (connected_servers[i] != alive_servers[i]) {
                        same = false;
                        break;
                    }
                }
                if (!same) {
                    break;
                }

                received_start = true;

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
				        SP_error(ret);
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

                // Initialize received_matrix[i] to false if server is in the current network component
                for (i = 0; i < 5; i++) {
                    if (connected_servers[i]) {
                        received_matrix[i] = false; 
                    } else {
                        received_matrix[i] = true; 
                    }
                }

                num_updates = 0;
                
                break;
            }

            case PARTICIPANTS_SERVER:
            {
                printf("Receive PARTICIPANTS_SERVER from server %s\n", sender);
                // message = <room_name> <server_index> <client1> <client2> ...

                if (!merging) {
                    printf("Warning: receive PARTICIPANTS_SERVER not in merging state\n");
                }

                if (!received_start) {
                    break;
                }

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

                if (!received_start) {
                    break;
                }

                // Parse 25 integers
                int temp_matrix[5][5];
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
                        temp_matrix[counter / 5][counter % 5] = temp_int;
                        counter++;
                        temp[0] = '\0';
                    }
                }
                if (counter != 25) {
                    printf("Error: did not receive 25 integers in MATRIX %s\n", message);
                    break;
                }

                // Adopt all integers if it is higher, except for line matrix[my_server_index]
                for (i = 0; i < 5; i++) {
                    // skip line with my server index
                    if (i + 1 == my_server_index) {
                        continue;
                    }
                    for (j = 0; j < 5; j++) {
                        if (temp_matrix[i][j] > matrix[i][j]) {
                            matrix[i][j] = temp_matrix[i][j];
                        }
                    }
                }

                // Parse server_index from sender
                int server_index; 
                ret = sscanf(sender, "#server%d#ugrad%*d", &server_index); 
                if (ret < 1) {
                    printf("Error: cannot parse server_index from sender %s\n", sender);
                    break;
                }
                received_matrix[server_index - 1] = true; 

                // Check if has received all matrices
                bool all_received = true; 
                for (i = 0; i < 5; i++) {
                    if (connected_servers[i] && !received_matrix[i]) {
                        all_received = false; 
                        break;
                    } 
                }

                // If just received expected number of matrices
                if (all_received) {
                    // Clear updates list
                    while(updates != NULL) { 
                        struct log * to_delete = updates; 
                        updates = updates->next; 
                        free(to_delete); 
                    }

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
                        // Clear logs[server_index] list up to the lowest index of all 5 servers
                        int lowest_index = matrix[0][j];
                        for (i = 0; i < 5; i++) {
                            if (matrix[i][j] < lowest_index) {
                                lowest_index = matrix[i][j];
                            }
                        }

                        ret = clear_log(&logs[j], &last_log[j], lowest_index);
                        if (ret < 0) {
                            printf("Error: fail to clear logs[%d] up to index %d\n", j, lowest_index);
                            break;
                        }
                        printf("Server: clear logs of server%d up to index %d\n", j + 1, lowest_index);

                        // Get lowest and highest index of all ACTIVE servers for this server
                        if (!connected_servers[my_server_index - 1]) {
                            printf("Error: this server%d is not in the current network component\n", my_server_index);
                            break;
                        }

                        lowest_index = matrix[my_server_index - 1][j];
                        int highest_index = matrix[my_server_index - 1][j];
                        int server_index = my_server_index;
                        for (i = 0; i < 5; i++) {
                            if (connected_servers[i]) {
                                if (matrix[i][j] > highest_index) {
                                    highest_index = matrix[i][j];
                                    server_index = i + 1;
                                }
                                if (matrix[i][j] < lowest_index) {
                                    lowest_index = matrix[i][j];
                                }
                            } 
                        }
                        
                        // Calculate num_updates expected to receive
                        num_updates += highest_index - lowest_index;

                        if (server_index != my_server_index) {
                            continue;
                        }

                        if (highest_index == 0) {
                            continue;
                        }

                        // Current server is responsible for sending missing updates for server j + 1

                        sent_updates[j] = lowest_index;
                        
                        // Send “UPDATE_MERGE <counter> <server_index> <index> <update>” to servers group with logs from lowest to highest index
                        struct log* cur = logs[j];
                        int num_sent = 0;
                        while (cur != NULL) {
                            if (cur->index > lowest_index) {
                                // Only send INIT_SEND_SIZE updates
                                if (num_sent < INIT_SEND_SIZE) {
                                    sprintf(to_send, "%d %d %d %s", cur->counter, cur->server_index, cur->index, cur->content);
                                    ret = SP_multicast(Mbox, AGREED_MESS, servers_group, UPDATE_MERGE, strlen(to_send), to_send);
                                    if (ret < 0) {
				                        SP_error( ret );
				                        Bye();
			                        }
                                    printf("Server: send log for reconcilation: %d %d %d %s\n", cur->counter, cur->server_index, cur->index, cur->content);
                                    num_sent++;
                                    sent_updates[j] = cur->index;

                                } else {
                                    break; 
                                }
                            }
                            cur = cur->next;
                        }
                    }

                    // if no updates to merge
                    if (num_updates == 0) {

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
                            ret = execute_buffer(buffer);
                            printf("Server: execute update in buffer: %s\n", buffer->content);
                            if (ret < 0) {
                                printf("Error: fail to execute update in buffer: %s\n", buffer->content);
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

                if (merging) {
                    struct log *new_log = malloc(sizeof(struct log));
                    // content = <update>
                    sprintf(new_log->content, "%s", message);
                    new_log->next = NULL;

                    // Append to buffer list
                    if (buffer == NULL) {
                        buffer = new_log;
                        end_of_buffer = new_log;
                    } else {
                        end_of_buffer->next = new_log;
                        end_of_buffer = new_log;
                    }

                    break;
                }

                // Increment counter
                my_counter++;

                // Increment index
                my_index++;

                // Write the message to log file
                // Append to logs[my_server_index] list
                // Update matrix[my_server_index][my_server_index] to new index
                ret = save_update(my_counter, my_server_index, my_index, message, true);
                if (ret < 0) {
                    printf("Error: fail to save my update to file in UPDATE_CLIENT %s\n", message);
                    break;
                }

                // Send “UPDATE_NORMAL <my_counter> <my_server_index> <my_index> <update>” to servers group
                char update[300];
                strcpy(update, message);
                sprintf(to_send, "%d %d %d %s", my_counter, my_server_index, my_index, update);
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
                num_update_normal++;
                // message = <counter> <server_index> <index> <update>

                int counter;
                int server_index;
                int index;
                char update[300];
                int num_read;

                ret = sscanf(message, "%d %d %d%n", &counter, &server_index, &index, &num_read);
                if (ret < 3) {
                    printf("Error: cannot parse timestamp and server_index from UPDATE_NORMAL %s\n", message);
                    break;
                }

                strcpy(update, &message[num_read + 1]);

                // If the update is not sent by myself
                if (server_index != my_server_index) {
                    // If index is out of order, return
                    if (index != matrix[my_server_index - 1][server_index - 1] + 1) {
                        printf("Server: update is out of order in UPDATE_NORMAL %s\n", message);
                        break;
                    }

                    // save to file and update matrix, counter, etc
                    ret = save_update(counter, server_index, index, update, true);
                    if (ret < 0) {
                        printf("Error: fail to save update in UPDATE_NORMAL %s\n", message);
                        break;
                    }
                }

                // Execute the update on data structures, and send messages to clients
                if (update[0] == 'a') {
                    ret = execute_append(counter, server_index, update);
                } else if (update[0] == 'l') {
                    ret = execute_like(counter, server_index, update);
                } else if (update[0] == 'r') {
                    ret = execute_unlike(counter, server_index, update);
                } else {
                    printf("Error: unknown command in UPDATE_NORMAL %s\n", message);
                    break;
                }

                if (ret < 0) {
                    printf("Error: fail to execute update in UPDATE_NORMAL %s\n", message);
                    break;
                }

                
                /* For every FREQ_SAVE UPDATE_NORMAL messages received,
                    save state to state file
                */
                if (num_update_normal == FREQ_SAVE) {
                    num_update_normal = 0;
                    state_fd = fopen(state_file_name, "w");

                    ret = write_state();
                    if (ret < 0) {
                        printf("Server: fail to write state to file\n");
                        break;
                    }

                    ret = fclose(state_fd);
                    if (ret < 0) {
                        printf("Error: fail to close state file\n");
                        break;
                    }
                }

                break;
            }

            case UPDATE_MERGE:
            {
                printf("Receive UPDATE_MERGE %s\n", message);
                // message = <counter> <server_index> <index> <update>

                if (!merging) {
                    printf("Warning: receive MATRIX not in merging state\n");
                }

                if (!received_start) {
                    break;
                }
                
                if (num_updates == 0) {
                    break; 
                }

                num_updates--; 

                int counter;
                int server_index;
                int index; 
                char update[300];
                int num_read;

                ret = sscanf(message, "%d %d %d%n", &counter, &server_index, &index, &num_read);
                if (ret < 2) {
                    printf("Error: cannot parse counter, server_index and index from UPDATE_MERGE %s\n", message);
                    break;
                }

                strcpy(update, &message[num_read + 1]);

                // Parse server_index from sender
                int sender_server_index; 
                ret = sscanf(sender, "#server%d#ugrad%*d", &sender_server_index); 
                if (ret < 1) {
                    printf("Error: could not parse server_index from sender %s\n", sender);
                    break;
                }
                
                // If update is sent from myself
                if (sender_server_index == my_server_index) {

                    // If I have not sent all missing logs
                    if (sent_updates[server_index - 1] < matrix[my_server_index - 1][server_index - 1]) {

                        // Send one more update to servers group
                        struct log* cur = logs[server_index - 1];
                        while (cur != NULL) {
                            if (cur->index == index + 1) {
                                // Send “UPDATE_MERGE <counter> <server_index> <index> <update>”
                                sprintf(to_send, "%d %d %d %s", cur->counter, cur->server_index, cur->index, cur->content);
                                ret = SP_multicast(Mbox, AGREED_MESS, servers_group, UPDATE_MERGE, strlen(to_send), to_send);
                                if (ret < 0) {
				                    SP_error( ret );
				                    Bye();
			                    }
                                printf("Server: send log for reconcilation: %d %d %d %s\n", cur->counter, cur->server_index, cur->index, cur->content);
                                sent_updates[server_index - 1] = cur->index;
                                break;
                            }
                            cur = cur->next;
                        }
                    }
                }

                // If update does not exist
                if (index > matrix[my_server_index - 1][server_index - 1]) {
                    struct log *new_log = malloc(sizeof(struct log));
                    new_log->counter = counter;
                    new_log->server_index = server_index;
                    new_log->index = index;
                    strcpy(new_log->content, update);
                    new_log->next = NULL;

                    // Insert it in updates list in the order of counter+server_index
                    insert_log(&updates, new_log);
                }

                // if received all missing updates
                if (num_updates == 0) {

                    printf("Server: matrix = \n");
                    for (i = 0; i < 5; i++) {
                        printf("\t");
                        for (int j = 0; j < 5; j++) {
                            printf("%d ", matrix[i][j]);
                        }
                        printf("\n");
                    }
                    
                    // execute every update in updates list
                    while(updates != NULL) {

                        // Save update to file, append in logs list, update counter/matrix accordingly
                        ret = save_update(updates->counter, updates->server_index, updates->index, updates->content, true);
                        if (ret < 0) {
                            printf("Error: fail to save update received during merge: %d %d %d %s\n", updates->counter, updates->server_index, updates->index, updates->content);
                        }

                        printf("Server: execute update received during merge: %d %d %d %s\n", updates->counter, updates->server_index, updates->index, updates->content);
                        if (updates->content[0] == 'a') {
                            execute_append(updates->counter, updates->server_index, updates->content);
                        } else if (updates->content[0] == 'l') {
                            execute_like(updates->counter, updates->server_index, updates->content);
                        } else if(updates->content[0] == 'r') {
                            execute_unlike(updates->counter, updates->server_index, updates->content);
                        } else {
                            printf("Error: unknown command %s in updates list\n", updates->content); 
                        }

                        struct log* to_delete = updates; 
                        updates = updates->next; 
                        free(to_delete);
                    }

                    // Mark as out of merging state
                    merging = false;
                    printf("Server: Finish merging\n");

                    // Execute updates received in buffer during merging
                    while (buffer != NULL) {
                        ret = execute_buffer(buffer);
                        printf("Server: execute update in buffer: %s\n", buffer->content);
                        if (ret < 0) {
                            printf("Error: fail to execute update in buffer: %s\n", buffer->content);
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

            case VIEW:
            {
                // Send “VIEW <5 numbers 0/1>” to the client’s private group
                sprintf(to_send, "%d %d %d %d %d", connected_servers[0], connected_servers[1],
                    connected_servers[2], connected_servers[3], connected_servers[4]);
                ret = SP_multicast(Mbox, AGREED_MESS, sender, VIEW, strlen(to_send), to_send);
                if (ret < 0) {
                    SP_error(ret);
                    Bye();
                }
                break;
            }

            case HISTORY:
            {
                // message = <room_name>
                ret = sscanf(message, "%s", room_name);
                if (ret < 1) {
                    printf("Error: fail to parse room_name from HISTORY %s\n", message);
                    break;
                }

                if (merging) {
                    struct log *new_log = malloc(sizeof(struct log));
                    // content = h <room_name> <client_name>
                    sprintf(new_log->content, "h %s %s", room_name, sender);
                    new_log->next = NULL;

                    // Append to buffer list
                    if (buffer == NULL) {
                        buffer = new_log;
                        end_of_buffer = new_log;
                    } else {
                        end_of_buffer->next = new_log;
                        end_of_buffer = new_log;
                    }
                    break;
                }

                // update = <room_name> <client_name>
                char update[300];
                sprintf(update, "h %s %s", room_name, sender);
                ret = execute_history(update);
                if (ret < 0) {
                    printf("Error: fail to execute HISTORY %s\n", message);
                }

                break;

            }
            
            default:
            {
                printf("Warning: receive unknown message type\n");
                break;
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

                /* TODO: If there is a server with the same server index
                        Print error messages that server index needs to be unique
                        Quit the program */

                merging = true; 
                received_start = false;

                // Find servers in the current network component
                for (i = 0; i < 5; i++) {
                    connected_servers[i] = false;
                }

                printf("Server: current network component has\n");

                int server_index;
                for (i = 0; i < num_groups; i++) {
                    printf("\t%s\n", &target_groups[i][0]);
                    ret = sscanf(&target_groups[i][0], "#server%d#ugrad%*d", &server_index);
                    if (ret < 1) {
                        printf("Error: cannot parse server index from %s\n", &target_groups[i][0]);
                    } else {
                        connected_servers[server_index - 1] = true;
                    }
                }

                // If I have the lowest server_index in the current network component, send “START <5 integers 0/1>”
                int lowest_server_index;
                for (int i = 0; i < 5; i++) {
                    if (connected_servers[i]) {
                        lowest_server_index = i + 1; 
                        break;
                    }
                }
                if (lowest_server_index == my_server_index) {
                    // Send "START <5 integers 0/1>"
                    sprintf(to_send, "%d %d %d %d %d", connected_servers[0], connected_servers[1],
                        connected_servers[2], connected_servers[3], connected_servers[4]);
                    
                    ret = SP_multicast(Mbox, AGREED_MESS, servers_group, START, strlen(to_send), to_send);
                    if (ret < 0) {
				        SP_error( ret );
				        Bye();
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
    clear();

    SP_disconnect(Mbox);
	exit(0);
}

static int read_state()
{
    char *line = malloc(MAX_MESS_LEN);
    size_t length = MAX_MESS_LEN;
    char content[81];
    int num_rooms;
    int num_messages;
    int counter;
    int server_index;
    int len_likes;
    int liked;
    int ret;

    // Read first line: <counter>
    ret = getline(&line, &length, state_fd);
    if (ret < 0) {
        printf("Error: fail to read first line of state file %s\n", state_file_name);
        free(line);
        return -1;
    }
    ret = sscanf(line, "%d", &counter);
    if (ret < 1) {
        printf("Error: fail to read counter in first line %s\n", line);
        free(line);
        return -1;
    }

    // Set my counter
    my_counter = counter;

    // Read second line: <5 indices>
    ret = getline(&line, &length, state_fd);
    if (ret < 0) {
        printf("Error: fail to read second line of state file %s\n", state_file_name);
        free(line);
        return -1;
    }
    ret = sscanf(line, "%d %d %d %d %d", &matrix[my_server_index - 1][0], &matrix[my_server_index - 1][1],
        &matrix[my_server_index - 1][2], &matrix[my_server_index - 1][3], &matrix[my_server_index - 1][4]);
    if (ret < 5) {
        printf("Error: fail to read 5 lamport counters from first line %s\n", line);
        free(line);
        return -1;
    }
    printf("\tRetrive 5 indices: %d %d %d %d %d\n", matrix[my_server_index - 1][0], matrix[my_server_index - 1][1],
        matrix[my_server_index - 1][2], matrix[my_server_index - 1][3], matrix[my_server_index - 1][4]);

    // Set my index
    my_index = matrix[my_server_index - 1][my_server_index - 1];

    // Read third line: <number of rooms>
    ret = getline(&line, &length, state_fd);
    if (ret < 0) {
        printf("Error: fail to read second line of state file %s\n", state_file_name);
        free(line);
        return -1;
    }
    ret = sscanf(line, "%d", &num_rooms);
    if (ret < 1) {
        printf("Error: fail to read number of rooms in line %s\n", line);
        free(line);
        return -1;
    }

    // Read room lines
    for (int i = 0; i < num_rooms; i++) {
        // Read <room_name> <num_messages>
        ret = getline(&line, &length, state_fd);
        if (ret < 0) {
            printf("Error: fail to read first line of room in state file %s\n", state_file_name);
            free(line);
            return -1;
        }
        ret = sscanf(line, "%s %d", room_name, &num_messages);
        if (ret < 2) {
            printf("Error: fail to read room_name and num_messages in line %s\n", line);
            free(line);
            return -1;
        }

        struct room *room = create_room(&rooms, room_name);
        printf("\tCreate room %s with %d messages\n", room_name, num_messages);

        for (int j = 0; j < num_messages; j++) {
            // Read message line: <counter> <server_index> <creator> <length of likes list> <content>
            ret = getline(&line, &length, state_fd);
            if (ret < 0) {
                printf("Error: fail to read message line in state file %s\n", state_file_name);
                free(line);
                return -1;
            }
            ret = sscanf(line, "%d %d %s %d %[^\n]\n", &counter, &server_index, username, &len_likes, content);
            if (ret < 5) {
                printf("Error: fail to parse counter, server_index, creator, length of likes list and num_likes from line %s\n", line);
                free(line);
                return -1;
            }

            struct message *new_message = malloc(sizeof(struct message));
            new_message->counter = counter;
            new_message->server_index = server_index;
            strcpy(new_message->content, content);
            strcpy(new_message->creator, username);
            new_message->likes = NULL;
            new_message->next = NULL;

            ret = insert_message(room, new_message);
            if (ret < 0) {
                printf("Error: fail to insert message to room %s\n", room_name);
                free(line);
                return -1;
            }
            printf("\t\tinsert message created by %s: %s\n", new_message->creator, new_message->content);

            for (int k = 0; k < len_likes; k++) {
                // Read like line: <username> <counter> <server_index> <liked 0/1>
                ret = getline(&line, &length, state_fd);
                if (ret < 0) {
                    printf("Error: fail to read like line in state file %s\n", state_file_name);
                    free(line);
                    return -1;
                }
                ret = sscanf(line, "%s %d %d %d", username, &counter, &server_index, &liked);
                if (ret < 4) {
                    printf("Error: fail to parse username, counter, server_index and liked from line %s\n", line);
                    free(line);
                    return -1;
                }

                if (liked == 0) {
                    ret = add_like(new_message, username, false, counter, server_index);
                } else if (liked == 1) {
                    ret = add_like(new_message, username, true, counter, server_index);
                } else {
                    printf("Error: liked is neither 0 nor 1 in line %s\n", line);
                    free(line);
                    return -1;
                }
                if (ret < 0) {
                    printf("Error: fail to add like node in line %s\n", line);
                    return -1;
                }
                printf("\t\t\tadd like node (%d) from %s\n", liked, username);
            }
        }
    }

    free(line);
    return 0;
}

static int write_state() {
    int ret;

    // Write first line: <counter>
    ret = fprintf(state_fd, "%d\n", my_counter);
    if (ret < 0) {
        printf("Error: fail to write counter to state file\n");
        return -1;
    }

    // Write second line: <5 indices>
    for (int i = 0; i < 5; i++) {
        ret = fprintf(state_fd, "%d ", matrix[my_server_index - 1][i]);
        if (ret < 0) {
            printf("Error: fail to write 5 counters to state file\n");
            return -1;
        }
    }

    // Write third line: <num_rooms>
    struct room* cur_room = rooms;
    int num_rooms = 0;
    while(cur_room != NULL) {
        num_rooms++;
        cur_room = cur_room->next;
    }
    ret = fprintf(state_fd, "\n%d\n", num_rooms);
    if (ret < 0) {
        printf("Error: fail to write num_rooms to state file %s\n", state_file_name);
        return -1;
    }

    // Write room lines
    cur_room = rooms;
    while(cur_room != NULL) {

        // Write <room_name> <num_messages>
        int num_messages = 0;
        struct message* cur_message = cur_room->messages;
        while(cur_message != NULL) {
            num_messages++; 
            cur_message = cur_message->next; 
        }
        ret = fprintf(state_fd, "%s %d\n", cur_room->name, num_messages);
        if (ret < 0) {
            printf("Error: fail to write <room_name> <num_messages> to state file %s\n", state_file_name);
            return -1;
        }

        cur_message = cur_room->messages;
        while(cur_message != NULL) {

            // Find length of likes list
            int len_likes = 0;
            struct like *cur_like = cur_message->likes;
            while (cur_like != NULL) {
                len_likes++;
                cur_like = cur_like->next;
            }

            // Write message line: <counter> <server_index> <creator> <length of likes list> <content>
            ret = fprintf(state_fd, "%d %d %s %d %s\n", cur_message->counter, cur_message->server_index,
                cur_message->creator, len_likes, cur_message->content);
            if (ret < 0) {
                printf("Error: fail to write message line to state file %s\n", state_file_name);
                return -1;
            }

            // Write <liked_by>
            cur_like = cur_message->likes;
            while(cur_like != NULL) {
                // Write like line: <username> <counter> <server_index> <liked 0/1>
                ret = fprintf(state_fd, "%s %d %d %d\n", cur_like->username, cur_like->counter, cur_like->server_index, cur_like->liked);
                if (ret < 0) {
                    printf("Error: fail to write likes list to state file %s\n", state_file_name);
                    return -1;
                }
                cur_like = cur_like->next;
            }

            cur_message = cur_message->next;
        }
        cur_room = cur_room->next;
    }

    return 0;
}

static int read_log(int i)
{
    char *line = malloc(MAX_MESS_LEN);
    size_t length = MAX_MESS_LEN;
    int ret;

    int counter;
    int server_index;
    int index;
    char update[300];
            
    while((ret = getline(&line, &length, log_fd[i])) != -1) {
        // line = <counter> <server_index> <index> <update>
        ret = sscanf(line, "%d %d %d %[^\n]\n", &counter, &server_index, &index, update);
        if (ret < 4) {
            printf("Error: cannot parse counter, server_index, index and update when reading from line %s\n", line);
            free(line);
            return -1;
        }

        if (server_index != i + 1) {
            printf("Warning: log file server%d-log%d.out has updates for server%d\n", my_server_index, i + 1, server_index);
            continue;
        }

        struct log* new_log = malloc(sizeof(struct log));
        new_log->counter = counter;
        new_log->server_index = server_index;
        new_log->index = index;
        strcpy(new_log->content, update);
        new_log->next = NULL;

        // Covered in state, no need to execute the update
        if (index <= matrix[my_server_index - 1][server_index - 1]) {
            // Append to logs[server_index] list
            if (logs[server_index - 1] == NULL) {
                logs[server_index - 1] = new_log;
                last_log[server_index - 1] = new_log;
            } else {
                last_log[server_index - 1]->next = new_log;
                last_log[server_index - 1] = new_log;
            }

        // Is not covered by state
        } else {
            // Insert in updates list
            insert_log(&updates, new_log);
        }
    }

    free(line);
    return 0;
}

static void clear()
{
    // delete rooms list
    while (rooms != NULL) {
        // delete messages list
        struct message *cur_message = rooms->messages;
        while (cur_message != NULL) {
            // delete likeds list
            struct like *cur_like = cur_message->likes;
            while (cur_like != NULL) {
                struct like *like_to_delete = cur_like;
                cur_like = cur_like->next;
                free(like_to_delete);
            }
            cur_message->likes = NULL;

            struct message *message_to_delete = cur_message;
            cur_message = cur_message->next;
            free(message_to_delete);
        }
        rooms->messages = NULL;

        // delete participants[i]
        for (int i = 0; i < 5; i++) {
            struct participant *cur_participant = rooms->participants[i];
            while (cur_participant != NULL) {
                struct participant *participant_to_delete = cur_participant;
                cur_participant = cur_participant->next;
                free(participant_to_delete);
            }
            rooms->participants[i] = NULL;
        }

        struct room *room_to_delete = rooms;
        rooms = rooms->next;
        free(room_to_delete);
    }

    rooms = NULL;

    // Set matrix, counter and index to 0
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            matrix[i][j] = 0;
        }
    }
    my_counter = 0;
    my_index = 0;
}

static int save_update(int counter, int server_index, int index, char* update, bool write_to_file)
{
    int ret;

    if (write_to_file) {
        // Write update to “server[my_server_index]-log[server_index].out” file
        log_fd[server_index - 1] = fopen(log_file_names[server_index - 1], "a+");
        // Write <counter> <server_index> <index> <log content>
        ret = fprintf(log_fd[server_index - 1], "%d %d %d %s\n", counter, server_index, index, update);
        if (ret < 0) {
            printf("Error: fail to write update %s to file %s\n", update, log_file_names[server_index - 1]);
            return -1;
        }
        fclose(log_fd[server_index - 1]);
    }

    // Append it in logs[server_index] list
    struct log* new_log = malloc(sizeof(struct log));
    new_log->counter = counter;
    new_log->server_index = server_index;
    new_log->index = index;
    strcpy(new_log->content, update);
    new_log->next = NULL;

    if (logs[server_index - 1] == NULL) {
        logs[server_index - 1] = new_log;
        last_log[server_index - 1] = new_log;
    } else {
        last_log[server_index - 1]->next = new_log;
        last_log[server_index - 1] = new_log;
    }

    // Adopt the counter if it is higher
    if (counter > my_counter) {
        my_counter = counter;
    }

    // Update matrix[my_server_index][server_index] to the new index
    matrix[my_server_index - 1][server_index - 1] = index;
    return 0;
}

static int execute_append(int counter, int server_index, char *update)
{
    int ret;
    char to_send[MAX_MESS_LEN];
    int num_read;

    // update = a <room_name> <username> <content>
    ret = sscanf(update, "a %s %s%n", room_name, username, &num_read);
    if (ret < 2) {
        printf("Error: cannot parse room_name and username from update %s\n", update);
        return -1;
    }

    // Insert message to messages list of the room, in the order of counter+server_index
    struct message *new_message = malloc(sizeof(struct message));
    new_message->counter = counter;
    new_message->server_index = server_index;
    strcpy(new_message->content, &update[num_read + 1]);
    strcpy(new_message->creator, username);
    new_message->likes = NULL;
    new_message->next = NULL;

    struct room* room = find_room(rooms, room_name);
    ret = insert_message(room, new_message);
    if (ret < 0) {
        printf("Error: fail to append message. %s does not exist\n", room_name);
        return -1;
    }

    printf("\tappend message created by %s to %s: %s\n", new_message->creator, room_name, new_message->content);

    if (room->participants[my_server_index - 1] != NULL) {
        // Send “APPEND <counter> <server_index> <username> <content>” to the server-room group
        sprintf(to_send, "%d %d %s %s", new_message->counter, new_message->server_index, new_message->creator, new_message->content);
        sprintf(server_room_group, "server%d-%s", my_server_index, room_name);
        ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, APPEND, strlen(to_send), to_send);
        if (ret < 0) {
            SP_error(ret);
            Bye();
        }
    }
    return 0;
}

static int execute_like(int counter, int server_index, char *update)
{
    int ret;
    int message_counter;
    int message_server_index;
    char to_send[MAX_MESS_LEN];

    // update = l <room_name> <counter of the liked message> <server_index of the liked message> <username>
    ret = sscanf(update, "l %s %d %d %s", room_name, &message_counter, &message_server_index, username);
    if (ret < 4) {
        printf("Error: cannot parse room_name, counter, server_index and username from update %s\n", update);
        return -1;
    }

    struct room *room = find_room(rooms, room_name);
    if (room == NULL) {
        printf("Error: cannot find room named %s\n", room_name);
        return -1;
    }

    struct message *message = find_message(room, message_counter, message_server_index);
    if (message == NULL) {
        printf("Error: cannot find message with counter %d and server_index %d in room %s\n", message_counter, message_server_index, room_name);
        return -1;
    }

    // Check if user is the creator
    if (strcmp(username, message->creator) == 0) {
        printf("\tmessage is created by %s and cannot be liked by the creator\n", username);
        return 0;
    }

    struct like *cur = message->likes;
    while (cur != NULL) {
        // If there is already a like node with the same username
        if (strcmp(username, cur->username) == 0) {
            if (counter < cur->counter || (counter == cur->counter && server_index < cur->server_index)) {
                printf("\tmessage has been liked/unliked(%d) by a later update with counter %d server_index %d\n", cur->liked, cur->counter, cur->server_index);
                return 0;
            }
            cur->counter = counter;
            cur->server_index = server_index;

            if (!cur->liked) {
                cur->liked = true;
                printf("\t%s likes message %s in %s\n", username, message->content, room_name);
                if (room->participants[my_server_index - 1] != NULL) {
                    int num_likes = get_num_likes(message);
                    // Send “LIKES <message’s counter> <message’s server_index> <num_likes>” to the server-room group
                    sprintf(to_send, "%d %d %d", message_counter, message_server_index, num_likes);
                    sprintf(server_room_group, "server%d-%s", my_server_index, room_name);
                    ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, LIKES, strlen(to_send), to_send);
                    if (ret < 0) {
                        SP_error(ret);
                        Bye();
                    }
                }
            } else {
                printf("\tmessage has been liked by %s\n", username);
            }

            return 0;
        }
        cur = cur->next;
    }

    // If there is no node with the same username, insert a new node
    ret = add_like(message, username, true, counter, server_index);
    if (ret < 0) {
        printf("Error: fail to add like node for update %s\n", update);
        return -1;
    }
    printf("\tadd like node of %s to message %s in %s\n", username, message->content, room_name);

    if (room->participants[my_server_index - 1] != NULL) {
        int num_likes = get_num_likes(message);
        // Send “LIKES <message’s counter> <message’s server_index> <num_likes>” to the server-room group
        sprintf(to_send, "%d %d %d", message_counter, message_server_index, num_likes);
        sprintf(server_room_group, "server%d-%s", my_server_index, room_name);
        ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, LIKES, strlen(to_send), to_send);
        if (ret < 0) {
            SP_error(ret);
            Bye();
        }
    }

    return 0;
}

static int execute_unlike(int counter, int server_index, char *update)
{
    int ret;
    int message_counter;
    int message_server_index;
    char to_send[MAX_MESS_LEN];

    // update = r <room_name> <counter of the liked message> <server_index of the liked message> <username>
    ret = sscanf(update, "r %s %d %d %s", room_name, &message_counter, &message_server_index, username);
    if (ret < 4) {
        printf("Error: cannot parse room_name, counter, server_index and username from update %s\n", update);
        return -1;
    }

    struct room *room = find_room(rooms, room_name);
    if (room == NULL) {
        printf("Error: cannot find room named %s\n", room_name);
        return -1;
    }

    struct message *message = find_message(room, message_counter, message_server_index);
    if (message == NULL) {
        printf("Error: cannot find message with counter %d and server_index %d in room %s\n", message_counter, message_server_index, room_name);
        return -1;
    }

    // Check if user is the creator
    if (strcmp(username, message->creator) == 0) {
        printf("\tmessage is created by %s and cannot be unliked by the creator\n", username);
        return 0;
    }

    struct like *cur = message->likes;
    while (cur != NULL) {
        // If there is already a like node with the same username
        if (strcmp(username, cur->username) == 0) {
            if (counter < cur->counter || (counter == cur->counter && server_index < cur->server_index)) {
                printf("\tmessage has been liked/unliked(%d) by a later update with counter %d server_index %d\n", cur->liked, cur->counter, cur->server_index);
                return 0;
            }
            cur->counter = counter;
            cur->server_index = server_index;

            if (cur->liked) {
                cur->liked = false;
                printf("\t%s unlikes message %s in %s\n", username, message->content, room_name);
                if (room->participants[my_server_index - 1] != NULL) {
                    int num_likes = get_num_likes(message);
                    // Send “LIKES <message’s counter> <message’s server_index> <num_likes>” to the server-room group
                    sprintf(to_send, "%d %d %d", message_counter, message_server_index, num_likes);
                    sprintf(server_room_group, "server%d-%s", my_server_index, room_name);
                    ret = SP_multicast(Mbox, AGREED_MESS, server_room_group, LIKES, strlen(to_send), to_send);
                    if (ret < 0) {
                        SP_error(ret);
                        Bye();
                    }
                }
            } else {
                printf("\tmessage has been unliked by %s\n", username);
            }

            return 0;
        }
        cur = cur->next;
    }

    // If there is no node with the same username, insert a new node
    ret = add_like(message, username, false, counter, server_index);
    if (ret < 0) {
        printf("Error: fail to add like node for update %s\n", update);
        return -1;
    }
    printf("\tadd unlike node of %s to message %s in %s\n", username, message->content, room_name);

    return 0;
}

static int execute_join(char *update)
{
    int ret;
    char sender[MAX_GROUP_NAME];
    char to_send[MAX_MESS_LEN];

    // update = <room_name> <client_name>
    ret = sscanf(update, "%s %s", room_name, sender);
    if (ret < 2) {
        printf("Error: fail to parse room_name and client_name from JOIN %s\n", update);
        return -1;
    }

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

    return 0;
}

static int execute_roomchange(char *update)
{
    int ret;
    char to_send[MAX_MESS_LEN];
    char old_room_name[80];
    char new_room_name[80];
    int server_index;

    // update = <client_name> <old_room> <new_room> <server_index>
    ret = sscanf(update, "%s %s %s %d", client_name, old_room_name, new_room_name, &server_index);
    if (ret < 4) {
        printf("Error: fail to parse client_name, old_room, new_room and server index from ROOMCHANGE %s\n", update);
        return -1;
    }

    if (strcmp(old_room_name, "null") != 0) {
        // Remove client from participants[server_index] in the old room
        struct room* old_room = find_room(rooms, old_room_name);
        if (old_room == NULL) {
            printf("Error: old %s does not exist", old_room_name);
            return -1;
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

    return 0;
}

static int execute_history(char *update)
{
    int ret;
    char sender[MAX_GROUP_NAME];
    char to_send[MAX_MESS_LEN];

    // update = <room_name> <client_name>
    ret = sscanf(update, "%s %s", room_name, sender);
    if (ret < 2) {
        printf("Error: cannot parse room_name and client_name in update %s\n", update);
        return -1;
    }

    struct room *room = find_room(rooms, room_name);
    if (room == NULL) {
        printf("Error: room %s does not exist\n", room_name);
        return -1;
    }

    struct message *cur = room->messages;
    int num_likes;
    while (cur != NULL) {
        // Calculate number of likes
        num_likes = get_num_likes(cur);

        // Send "HISTORY <creator> <num_likes> <content>" to client's private group
        sprintf(to_send, "%s %d %s", cur->creator, num_likes, cur->content);
        ret = SP_multicast(Mbox, AGREED_MESS, sender, HISTORY, strlen(to_send), to_send);
        if (ret < 0) {
            SP_error(ret);
            Bye();
        }
        cur = cur->next;
    }

    return 0;

}

static int execute_buffer(struct log *log)
{
    int ret = 0;

    if (log->content[0] == 'j') {
        // content = j <room_name> <client_name>
        ret = execute_join(&log->content[2]);

    } else if (log->content[0] == 'R') {
        // content = R <client_name> <old_room> <new_room> <server_index>
        ret = execute_roomchange(&log->content[2]);

    } else if (log->content[0] == 'h') {
        // content = h <room_name> <client_name>
        ret = execute_history(&log->content[2]);

    } else if (log->content[0] == 'a' || log->content[0] == 'l' || log->content[0] == 'r') {
        
        char to_send[MAX_MESS_LEN];

        // content = <update>

        // Increment counter
        my_counter++;

        // Increment index
        my_index++;

        // Write the message to log file
        // Append to logs[my_server_index] list
        // Update matrix[my_server_index][my_server_index] to new index
        ret = save_update(my_counter, my_server_index, my_index, log->content, true);
        if (ret < 0) {
            printf("Error: fail to save update to file: %d %d %d %s\n",
                my_counter, my_server_index, my_index, log->content);
            return -1;
        }

        // Send “UPDATE_NORMAL <my_counter> <my_server_index> <my_index> <update>” to servers group
        sprintf(to_send, "%d %d %d %s", my_counter, my_server_index, my_index, log->content);
        ret = SP_multicast(Mbox, AGREED_MESS, servers_group, UPDATE_NORMAL, strlen(to_send), to_send);
        if (ret < 0) {
            SP_error(ret);
            Bye();
        }

    } else {
        printf("Error: unknown log in buffer %s\n", log->content);
        return -1;
    }

    return ret;
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
        if ((cur->next->counter > message->counter) 
            || (cur->next->counter == message->counter && cur->next->server_index > message->server_index)) {
            
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

struct message* find_message(struct room *room, int counter, int server_index)
{
    if (room == NULL || room->messages == NULL) {
        return NULL;
    }

    struct message *cur = room->messages;
    while (cur != NULL) {
        if (cur->counter == counter && cur->server_index == server_index) {
            return cur;
        }
        cur = cur->next;
    }

    return NULL;
}

int get_num_likes(struct message *message)
{
    if (message == NULL) {
        return -1;
    }

    int num_likes = 0;

    struct like *cur = message->likes;
    while (cur != NULL) {
        if (cur->liked) {
            num_likes++;
        }
        cur = cur->next;
    }

    return num_likes;
}

int add_like(struct message *message, char* username, bool liked, int counter, int server_index)
{
    if (message == NULL) {
        return -1;
    }

    struct like *new_like = malloc(sizeof(struct like));
    strcpy(new_like->username, username);
    new_like->liked = liked;
    new_like->counter = counter;
    new_like->server_index = server_index;
    new_like->next = NULL;

    if (message->likes == NULL) {
        message->likes = new_like;
        return 0;
    }

    struct like dummy;
    dummy.next = message->likes;

    struct like *cur = &dummy;

    while (cur->next != NULL) {
        cur = cur->next;
    }

    cur->next = new_like;

    return 0;
}

void get_messages(char* to_send, struct room* room) {
    if (room == NULL || room->messages == NULL) {
        to_send[0] = '\0';
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
        int num_likes = get_num_likes(first);
        // Every message: <counter> <server_index> <creator> <num_likes> <content>\n
        sprintf(message, "%d %d %s %d %s\n", first->counter, first->server_index, first->creator, num_likes, first->content);
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

int clear_log(struct log **logs_ref, struct log **last_log_ref, int index)
{
    if (index == 0) {
        return 0;
    }

    if (*logs_ref == NULL || *last_log_ref == NULL) {
        return 0;
    }

    struct log *cur = *logs_ref;
    while (cur != NULL) {
        if (cur->index <= index) {
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

void insert_log(struct log **updates_ref, struct log *log)
{
    struct log dummy;
    dummy.next = *updates_ref;
    struct log *cur = &dummy;

    while (cur->next != NULL) {
        
        if (cur->next->counter > log->counter 
            || (cur->next->counter == log->counter && cur->next->server_index > log->server_index)) {

            // insert between cur and cur->next
            log->next = cur->next;
            cur->next = log;

            *updates_ref = dummy.next;
            return;
        }
        cur = cur->next;
    }

    // append at the end of list
    log->next = NULL;
    cur->next = log;
    *updates_ref = dummy.next;
    return;
}