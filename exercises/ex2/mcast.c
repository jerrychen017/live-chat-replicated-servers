#include <stdbool.h>
#include "net_include.h"
#include "packet.h"
#include "helper.h"
#include "recv_dbg.h"

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
    // initialize table entries to packets with empty tag
    for (int i = 0; i < num_machines; i++) {
        for (int j = 0; j < WINDOW_SIZE; j++) {
            table[i][j].tag = TAG_EMPTY;
        }
    }

    /* To store received packet in the array for each machine
    *  the index of the actual first entry
    */
    int start_array_indices[num_machines];
    memset(start_array_indices, 0, num_machines * sizeof(int)); // initializing start_array_indices

    // corresponding packet index for each start array index
    int start_packet_indices[num_machines];
    memset(start_packet_indices, 1, num_machines * sizeof(int)); // initializing start_packet_indices

    int last_delivered_indices[num_machines];
    memset(last_delivered_indices, 0, num_machines * sizeof(int)); // initializing last_delivered indices
    
    int end_indices[num_machines]; 
    memset(end_indices, -1, num_machines * sizeof(int)); 
    end_indices[machine_index - 1] = num_packets;

    // array of boolean indicating finished machines 
    bool finished[num_machines]; 
    memset(finished, false, num_machines * sizeof(bool)); 

    int counter = 0; 
    int last_delivered_counter = 0;

    // Receive START packet
    bytes_received = recv( sr, &received_packet, sizeof(struct packet), 0 );
    if (bytes_received != sizeof(struct packet)) {
        printf("Warning: number of bytes in the received pakcet does not equal to size of packet");
    }

    // initialize created_packets
    int num_created = 0;
    while (num_created < num_packets && num_created < WINDOW_SIZE) {
        created_packets[num_created].tag = TAG_DATA;
        counter++;
        created_packets[num_created].counter = counter;
        created_packets[num_created].machine_index = machine_index;
        created_packets[num_created].packet_index = num_created + 1;
        created_packets[num_created].random_data = (rand() % 999999) + 1;
        sendto( ss, &created_packets[num_created], sizeof(struct packet), 0, 
            (struct sockaddr *)&send_addr, sizeof(send_addr) );
        num_created++;
    }

    struct packet end_packet;
    end_packet.tag = TAG_END;
    end_packet.machine_index = machine_index;
    // put last packet_index
    end_packet.packet_index = num_packets;

    if (num_created == num_packets) {
        sendto(ss, &end_packet, sizeof(struct packet), 0,
            (struct sockaddr *)&send_addr, sizeof(send_addr) );
    }

    struct packet nack_packet;
    nack_packet.tag = TAG_NACK;
    nack_packet.machine_index = machine_index;
    memset(nack_packet.payload, -1, num_machines * sizeof(int));

    // file pointer for writing
    FILE *fd;
    char filename[7];
    filename[6] = '\0';
    sprintf(filename, "%d.out", machine_index);
    fd = fopen(filename, "w");

    for(;;) {
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, NULL, NULL, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                bytes_received = recv_dbg( sr, (char *) &received_packet, sizeof(struct packet), 0 );                
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
                        // checks if the target spot is occupied
                        if (table[received_packet.machine_index - 1][insert_index].tag != TAG_EMPTY) {
                            memcpy(&table[received_packet.machine_index - 1][insert_index], &received_packet, sizeof(struct packet));
                        }

                    
                        // try to deliver packets
                        bool is_full = true; 
                        while(is_full) {
                            bool deliverable[num_machines]; 
                            memset(start_array_indices, false, num_machines * sizeof(bool));
                            for (int i = 0; i < num_machines; i++) {
                                if (finished[i]) { // skips this machine if it has already ended
                                    nack_packet.payload[i] = -1; 
                                    continue; 
                                }
                                if (i != machine_index - 1) { // other machine case
                                    if (table[i][start_array_indices[i]].tag != TAG_EMPTY) {
                                        deliverable[i] = (table[i][start_array_indices[i]].counter == last_delivered_counter + 1);
                                        nack_packet.payload[i] = -1; 
                                    } else {
                                        deliverable[i] = false; 
                                        is_full = false; 
                                        nack_packet.payload[i] = start_packet_indices[i]; 
                                    }
                                } else { // my machine case 
                                    deliverable[i] = true; 
                                }
                            }
                            if (is_full) {
                                // deliver
                                for (int i = 0; i < num_machines; i++) {
                                    if (finished[i]) { // skips this machine if it has already ended
                                        continue; 
                                    }
                                    if (deliverable[i]) {
                                        if (i + 1 == machine_index) { // my machine case
                                            fprintf(fd, "%2d, %8d, %8d\n", machine_index, start_packet_indices[i], created_packets[start_array_indices[i]].random_data);
                                            // update my ack, which represents delivered upto 
                                            acks[i]++;
                                            
                                            // TODO: how to end?
                                            check_end(fd, acks, finished, num_machines, machine_index, num_packets);

                                            int min = acks[0]; 
                                            for (int j = 0; j < num_machines; j++) {
                                                if (acks[j] < min) {
                                                    min = acks[j];
                                                }
                                            }
                                            if (min - start_packet_indices[i] > 1) {
                                                printf("Warning: min(ack) increase more than one after delivering my packet\n");
                                            }
                                            if (min >= start_packet_indices[i]) {

                                                // create new packet
                                                created_packets[start_array_indices[i]].tag = TAG_DATA;
                                                counter++;
                                                created_packets[start_array_indices[i]].counter = counter;
                                                created_packets[start_array_indices[i]].machine_index = machine_index;
                                                created_packets[start_array_indices[i]].packet_index = num_created + 1;
                                                num_created++;
                                                created_packets[start_array_indices[i]].random_data = (rand() % 999999) + 1;

                                                sendto( ss, &created_packets[start_array_indices[i]], sizeof(struct packet), 0, 
                                                    (struct sockaddr *)&send_addr, sizeof(send_addr) );

                                                if (num_created == num_packets) {
                                                    sendto(ss, &end_packet, sizeof(struct packet), 0,
                                                        (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                                }

                                                // slide window
                                                start_array_indices[i] = (start_array_indices[i] + 1) % WINDOW_SIZE;
                                                start_packet_indices[i]++; 

                                            }
                                            
                                        } else { // other machine case
                                            if (i + 1 != table[i][start_array_indices[i]].machine_index) {
                                                printf("Warning: variable i doesn't match with the machine index in the table\n");
                                            }
                                            if (start_packet_indices[i] != table[i][start_array_indices[i]].packet_index) {
                                                printf("Warning: packet index doesn't match\n");
                                            }
                                            fprintf(fd, "%2d, %8d, %8d\n", i + 1, start_packet_indices[i], table[i][start_array_indices[i]].random_data); 

                                            // check if the machine has finished. update the finished array if yes. 
                                            if (end_indices[i] != -1 && start_packet_indices[i] == end_indices[i]) { // finished 
                                                finished[i] = true;
                                                // TODO: how to end?
                                                check_end(fd, acks, finished, num_machines, machine_index, num_packets);
                                            } else {
                                                // discard delivered packet in table
                                                table[i][start_array_indices[i]].tag = TAG_EMPTY;
                                                start_array_indices[i] = (start_array_indices[i] + 1) % WINDOW_SIZE;
                                                start_packet_indices[i]++;
                                            }
                                            
                                        } // end of 
                                    }
                                } // end of deliver for loop  
                            } else { // not full, we have missing packets, send nack
                                sendto(ss, &nack_packet, sizeof(struct packet), 0,
                                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
                            }
                        } // end of while loop 
                        
                        // send ack
                        struct packet ack_packet; 
                        ack_packet.tag = TAG_ACK; 
                        ack_packet.machine_index = machine_index;
                        for (int i = 0; i < num_machines; i++) {
                            ack_packet.payload[i] = start_packet_indices[i] - 1;
                        }
                        sendto(ss, &ack_packet, sizeof(struct packet), 0,
                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                        break;
                    }

                    case TAG_ACK:
                    {
                        acks[received_packet.machine_index - 1] = received_packet.payload[machine_index - 1];
                        // TODO: how to end?!!!!
                        check_end(fd, acks, finished, num_machines, machine_index, num_packets);
                        break;
                    }

                    case TAG_NACK:
                    {
                        for (int i = 0; i < num_machines; i++) {
                            int requested_packet_index = received_packet.payload[i];
                            if (requested_packet_index != -1) {
                                if (requested_packet_index > start_packet_indices[machine_index - 1] + WINDOW_SIZE - 1) {
                                    continue;
                                }
                                if (i == machine_index - 1) { // my machine case
                                    if (requested_packet_index == num_packets + 1) {
                                        sendto(ss, &end_packet, sizeof(struct packet), 0,
                                        (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    } else {
                                        int index = convert(requested_packet_index, start_packet_indices[machine_index - 1], start_array_indices[machine_index - 1]);
                                        sendto(ss, &created_packets[index], sizeof(struct packet), 0,
                                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    }
                                } else { // other machine case
                                    // TODO: help other machines send end packet index
                                    int index = convert(requested_packet_index, start_packet_indices[machine_index - 1], start_array_indices[machine_index - 1]);
                                    if (table[i][index].tag != TAG_EMPTY) {
                                        sendto(ss, &table[i][index], sizeof(struct packet), 0,
                                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    }
                                }
                            }
                        }
                        // TODO: try sending using unicast 
                        break;
                    }

                    case TAG_END:
                    {
                        end_indices[received_packet.machine_index - 1] = received_packet.packet_index;
                        break;
                    }
                }

            } else {
                // timeout
                // send ack
                struct packet ack_packet; 
                ack_packet.tag = TAG_ACK; 
                ack_packet.machine_index = machine_index;
                for (int i = 0; i < num_machines; i++) {
                    ack_packet.payload[i] = start_packet_indices[i] - 1;
                }
                sendto(ss, &ack_packet, sizeof(struct packet), 0,
                    (struct sockaddr *)&send_addr, sizeof(send_addr) ); 

                bool all_negative = true;
                for (int i = 0; i < num_machines; i++) {
                    if (nack_packet.payload[i] != -1) {
                        all_negative = false;
                        break;
                    }
                }
                if (!all_negative) {
                    sendto(ss, &nack_packet, sizeof(struct packet), 0,
                        (struct sockaddr *)&send_addr, sizeof(send_addr) ); 
                }   
            }
        }
    }

    return 0;

}