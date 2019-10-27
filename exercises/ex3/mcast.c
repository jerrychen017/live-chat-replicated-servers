#include "message.h"
#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char User[80];
static char Spread_name[80];

static char Private_group[MAX_GROUP_NAME];
static char groups[10][MAX_GROUP_NAME];
static mailbox Mbox;
static int Num_sent;
static unsigned int Previous_len;
static int To_exit = 0;

static int num_messages;
static int num_processes;
static int process_index;
static int num_sent = 0;

#define MAX_MESSLEN 102400
#define MAX_VSSETS 10
#define MAX_MEMBERS 100

static void receive_messages();
static void Bye();

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        printf("Mcast usage: mcast <num_of_messages> <process_index> "
               "<number_of_processes>\n");
        exit(1);
    }

    num_messages = atoi(argv[1]);
    if (num_messages < 0)
    {
        perror("Mcast: invalid number of messages.\n");
        exit(1);
    }

    num_processes = atoi(argv[3]);
    if (num_processes < 0 || num_processes > 10)
    {
        perror("Mcast: invalid number of processes.\n");
        exit(1);
    }

    process_index = atoi(argv[2]);
    if (!(process_index >= 1 && process_index <= num_processes))
    {
        perror("Mcast: invalid number of processes or invalid process index.\n");
        exit(1);
    }

    sprintf(User, "user_jerry");
    sprintf(Spread_name, "4803");

    int ret;
    int mver, miver, pver;
    sp_time test_timeout;

    test_timeout.sec = 5;
    test_timeout.usec = 0;

    if (!SP_version(&mver, &miver, &pver))
    {
        printf("main: Illegal variables passed to SP_version()\n");
        Bye();
    }

    ret = SP_connect_timeout(Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout);
    if (ret != ACCEPT_SESSION)
    {
        SP_error(ret);
        Bye();
    }

    E_init();

    E_attach_fd(Mbox, READ_FD, receive_messages, 0, NULL, HIGH_PRIORITY);

    char group[80] = "jerrys_group";
    sprintf(groups[0], "jerrys_group");
    ret = SP_join(Mbox, group);
    if (ret < 0)
    {
        SP_error(ret);
    }

    E_handle_events();
}

static void receive_messages()
{
    // static char mess[MAX_MESSLEN];
    struct message mess;
    char sender[MAX_GROUP_NAME];
    char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
    membership_info memb_info;
    vs_set_info vssets[MAX_VSSETS];
    unsigned int my_vsset_index;
    int num_vs_sets;
    char members[MAX_MEMBERS][MAX_GROUP_NAME];
    int num_groups;
    int service_type;
    int16 mess_type;
    int endian_mismatch;
    int i, j;
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

    if (ret < 0)
    {
        if (!To_exit)
        {
            SP_error(ret);
            printf("\n============================\n");
            printf("\nBye.\n");
        }
        exit(0);
    }

    if (Is_regular_mess(service_type))
    {
        // mess[ret] = 0;
        if (Is_agreed_mess(service_type))
        {
            printf("received AGREED ");
            printf("mess process index is : %d\n", mess.process_index);
            // printf("message from %s, of type %d, (endian %d) to %d groups \n(%d bytes): %s\n",
            //        sender, mess_type, endian_mismatch, num_groups, ret, mess);
        }
        else
        {
            printf("Warning: didn't receive AGREED \n");
        }
    }
    else if (Is_membership_mess(service_type))
    {
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
            for (i = 0; i < num_groups; i++)
                printf("\t%s\n", &target_groups[i][0]);
            printf("grp id is %d %d %d\n", memb_info.gid.id[0], memb_info.gid.id[1], memb_info.gid.id[2]);

            if (num_groups == num_processes)
            {
                printf("Start sending data packets.\n");

                int num_to_send = num_processes;
                if (INIT_SEND_SIZE < num_to_send)
                {
                    num_to_send = INIT_SEND_SIZE;
                }

                struct message data_packet;
                for (int i = 0; i < num_to_send; i++)
                {
                    data_packet.tag = TAG_DATA;
                    data_packet.process_index = process_index;
                    data_packet.message_index = num_sent + 1;
                    data_packet.random_number = (rand() % 999999) + 1;

                    ret = SP_multigroup_multicast(Mbox, AGREED_MESS, 1, (const char(*)[MAX_GROUP_NAME])groups, 1, sizeof(struct message), (char *)&data_packet);

                    num_sent++;
                }
            }

            if (Is_caused_join_mess(service_type))
            {
                printf("Due to the JOIN of %s\n", memb_info.changed_member);
            }
            else if (Is_caused_leave_mess(service_type))
            {
                printf("Due to the LEAVE of %s\n", memb_info.changed_member);
            }
            else if (Is_caused_disconnect_mess(service_type))
            {
                printf("Due to the DISCONNECT of %s\n", memb_info.changed_member);
            }
            else if (Is_caused_network_mess(service_type))
            {
                printf("Due to NETWORK change with %u VS sets\n", memb_info.num_vs_sets);
                num_vs_sets = SP_get_vs_sets_info((char *)&mess, &vssets[0], MAX_VSSETS, &my_vsset_index);
                if (num_vs_sets < 0)
                {
                    printf("BUG: membership message has more then %d vs sets. Recompile with larger MAX_VSSETS\n", MAX_VSSETS);
                    SP_error(num_vs_sets);
                    exit(1);
                }
                for (i = 0; i < num_vs_sets; i++)
                {
                    printf("%s VS set %d has %u members:\n",
                           (i == my_vsset_index) ? ("LOCAL") : ("OTHER"), i, vssets[i].num_members);
                    ret = SP_get_vs_set_members((char *)&mess, &vssets[i], members, MAX_MEMBERS);
                    if (ret < 0)
                    {
                        printf("VS Set has more then %d members. Recompile with larger MAX_MEMBERS\n", MAX_MEMBERS);
                        SP_error(ret);
                        exit(1);
                    }
                    for (j = 0; j < vssets[i].num_members; j++)
                        printf("\t%s\n", members[j]);
                }
            }
        }
        else if (Is_transition_mess(service_type))
        {
            printf("received TRANSITIONAL membership for group %s\n", sender);
        }
        else if (Is_caused_leave_mess(service_type))
        {
            printf("received membership message that left group %s\n", sender);
        }
        else
            printf("received incorrecty membership message of type 0x%x\n", service_type);
    }
    else if (Is_reject_mess(service_type))
    {
        printf("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n",
               sender, service_type, mess_type, endian_mismatch, num_groups, ret, (char *)&mess);
    }
    else
        printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);
}

static void Bye()
{
    To_exit = 1;

    printf("\nBye.\n");

    SP_disconnect(Mbox);

    exit(0);
}