#include <stdbool.h>
#include "net_include.h"
#include "packet.h"
#include "helper.h"

int main(int argc, char* argv[]) {
    // TODO: error checking on atoi 

    // args error checking
    if (argc != 5) {
        printf("Mcast usage: mcast <num_of_packets> <machine_index> "
                "<number_of_machines> <loss_rate>\n");
        exit(1);
    }
    int num_packets = atoi(argv[1]); 
    if (num_packets < 0) {
        perror("Mcast: invalid number of packets.\n"); 
        exit(1);
    }

    int machine_index = atoi(argv[2]); 
    int num_machines = atoi(argv[3]);
    if (!(num_machines >= 1 && num_machines <= 10 && machine_index >= 1 && machine_index <= num_machines)) {
        perror("Mcast: invalid number of machines or invalid machine index.\n"); 
        exit(1);
    }

    int loss_rate = atoi(argv[4]);
    if (!(loss_rate >= 0 && loss_rate <= 20)) {
        perror("Mcast: loss_rate should be within range [0, "
                "20]\n");
        exit(1);
    }

    struct sockaddr_in my_address;
    int sr = socket(AF_INET, SOCK_DGRAM, 0); /* socket */
    if (sr<0) {
        perror("Mcast: socket");
        exit(1);
    }

    my_address.sin_family = AF_INET;
    my_address.sin_addr.s_addr = INADDR_ANY;
    my_address.sin_port = htons(PORT);

    if ( bind( sr, (struct sockaddr *)&my_address, sizeof(my_address) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    struct ip_mreq     mreq;
    int mcast_addr = 225 << 24 | 1 << 16 | 1 << 8 | 100; /* (225.1.1.100) */
    mreq.imr_multiaddr.s_addr = htonl( mcast_addr );
    /* the interface could be changed to a specific interface if needed */
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    // sr joins multicast group
    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, 
        sizeof(mreq)) < 0) {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    int ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    unsigned char ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, 
        sizeof(ttl_val)) < 0) {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val );
    }

    struct sockaddr_in send_addr;
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                num;

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;

    int bytes_received; 
    struct packet received_packet; 

    // internal data structures
    struct packet created_packets[WINDOW_SIZE];
    
    int acks[num_machines];
    memset(acks, 0, num_machines * sizeof(int)); // initializing acks

    struct packet table[num_machines][WINDOW_SIZE];

    /* To store received packet in the array for each machine
    *  the index of the actual first entry
    */
    int start_array_indices[num_machines];
    memset(start_array_indices, 0, num_machines * sizeof(int)); // initializing start_array_indices

    // corresponding packet index for each start array index
    int start_packet_indices[num_machines];
    memset(start_packet_indices, 0, num_machines * sizeof(int)); // initializing start_packet_indices

    int last_delivered_indices[num_machines];
    memset(last_delivered_indices, 0, num_machines * sizeof(int)); // initializing last_delivered indices
    
    int end_indices[num_machines]; 
    memset(end_indices, -1, num_machines * sizeof(int)); 
    end_indices[machine_index - 1] = num_packets;

    // array of boolean indicating finished machines 
    bool finished[num_machines]; 
    memset(finished, false, num_machines * sizeof(bool)); 

    int counter = 0; 

    // Receive START packet
    bytes_received = recv( sr, &received_packet, sizeof(struct packet), 0 );
    if (bytes_received != sizeof(struct packet)) {
        printf("Warning: number of bytes in the received pakcet does not equal to size of packet");
    }

    // initialize created_packets
    int i = 0;
    while (acks[machine_index - 1] < num_packets && i < WINDOW_SIZE) {
        created_packets[i].tag = TAG_DATA;
        counter++;
        created_packets[i].counter = counter;
        created_packets[i].machine_index = machine_index;
        acks[machine_index - 1]++;
        created_packets[i].packet_index = acks[machine_index - 1];
        created_packets[i].random_data = (rand() % 999999) + 1;
        i++;
        sendto( ss, &created_packets[i], sizeof(struct packet), 0, 
            (struct sockaddr *)&send_addr, sizeof(send_addr) );
    }

    struct packet end_packet;
    end_packet.tag = TAG_END;
    end_packet.machine_index = machine_index;
    // put last packet_index
    end_packet.packet_index = num_packets;

    if (acks[machine_index - 1] == num_packets) {
        sendto(ss, &end_packet, sizeof(struct packet), 0,
            (struct sockaddr *)&send_addr, sizeof(send_addr) );
    }


    for(;;) {
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, NULL, NULL, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                // TODO: change to recv_dbg
                bytes_received = recv( sr, &received_packet, sizeof(struct packet), 0 );                
                if (bytes_received != sizeof(struct packet)) {
                    printf("Warning: number of bytes in the received pakcet does not equal to size of packet");
                }

                switch (received_packet.tag) {
                    case TAG_START:
                    {
                        printf("Warning: receive START packet in the middle of delivery");
                        break;
                    }

                    case TAG_DATA:
                    {
                        // insert packet to table
                        int insert_index = convert(received_packet.packet_index, start_packet_indices[machine_index - 1], start_array_indices[machine_index - 1]);
                        memcpy(&table[received_packet.machine_index - 1][insert_index], &received_packet, sizeof(struct packet));
                        break;
                    }

                    case TAG_ACK:
                    {
                        break;
                    }

                    case TAG_NACK:
                    {
                        break;
                    }

                    case TAG_END:
                    {
                        break;
                    }
                }

            } else {
                // timeout

            }
        }
    }

    return 0;

}