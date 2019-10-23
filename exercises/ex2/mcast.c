#include <stdbool.h>
#include <sys/time.h>
#include "net_include.h"
#include "packet.h"
#include "helper.h"
#include "recv_dbg.h"

int main(int argc, char *argv[])
{
    // TODO: who responds to nack? (implement explicit flow control) unicast
    // makes sure after start packet, we can send IP address to all other machines
    // TODO: last big timeout

    // args error checking
    if (argc != 5)
    {
        printf("Mcast usage: mcast <num_of_packets> <machine_index> "
               "<number_of_machines> <loss_rate>\n");
        exit(1);
    }
    int num_packets = atoi(argv[1]);
    if (num_packets < 0)
    {
        perror("Mcast: invalid number of packets.\n");
        exit(1);
    }

    int machine_index = atoi(argv[2]);
    int num_machines = atoi(argv[3]);
    if (!(num_machines >= 1 && num_machines <= 10 && machine_index >= 1 && machine_index <= num_machines))
    {
        perror("Mcast: invalid number of machines or invalid machine index.\n");
        exit(1);
    }

    int loss_rate = atoi(argv[4]);
    if (!(loss_rate >= 0 && loss_rate <= 20))
    {
        perror("Mcast: loss_rate should be within range [0, "
               "20]\n");
        exit(1);
    }

    struct sockaddr_in my_address;
    int sr = socket(AF_INET, SOCK_DGRAM, 0); /* socket */
    if (sr < 0)
    {
        perror("Mcast: socket");
        exit(1);
    }

    my_address.sin_family = AF_INET;
    my_address.sin_addr.s_addr = INADDR_ANY;
    my_address.sin_port = htons(PORT);

    if (bind(sr, (struct sockaddr *)&my_address, sizeof(my_address)) < 0)
    {
        perror("Mcast: bind");
        exit(1);
    }

    struct ip_mreq mreq;
    int mcast_addr = 225 << 24 | 1 << 16 | 1 << 8 | 30; /* (225.1.1.30) */
    mreq.imr_multiaddr.s_addr = htonl(mcast_addr);
    /* the interface could be changed to a specific interface if needed */
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    // sr joins multicast group
    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq,
                   sizeof(mreq)) < 0)
    {
        perror("Mcast: problem in setsockopt to join multicast address");
    }

    int ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss < 0)
    {
        perror("Mcast: socket");
        exit(1);
    }

    unsigned char ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val,
                   sizeof(ttl_val)) < 0)
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val);
    }

    struct sockaddr_in send_addr;
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr); /* mcast address */
    send_addr.sin_port = htons(PORT);

    fd_set mask;
    fd_set dummy_mask, temp_mask;
    int num;

    FD_ZERO(&mask);
    FD_ZERO(&dummy_mask);
    FD_SET(sr, &mask);

    struct timeval timeout;

    int bytes_received;
    struct packet *received_packet = malloc(sizeof(struct packet));

    // internal data structures
    int buffer_size = TABLE_SIZE / num_machines;
    struct packet *created_packets[buffer_size];
    for (int i = 0; i < buffer_size; i++)
    {
        created_packets[i] = NULL;
    }

    int acks[num_machines];
    memset(acks, 0, num_machines * sizeof(int)); // initializing acks

    // a table that contains packet pointers
    struct packet *table[num_machines][buffer_size];
    // initialize table entries to packets with empty tag
    for (int i = 0; i < num_machines; i++)
    {
        for (int j = 0; j < buffer_size; j++)
        {
            table[i][j] = NULL; // initialize table entries to NULL
        }
    }

    /* To store received packet in the array for each machine
    *  the index of the actual first entry
    */
    int start_array_indices[num_machines];
    memset(start_array_indices, 0, num_machines * sizeof(int)); // initializing start_array_indices

    // corresponding packet index for each start array index
    int start_packet_indices[num_machines];
    // initializing start_packet_indices
    for (int i = 0; i < num_machines; i++)
    {
        start_packet_indices[i] = 1;
    }

    int end_indices[num_machines];
    for (int i = 0; i < num_machines; i++)
    {
        end_indices[i] = -1;
    }
    end_indices[machine_index - 1] = num_packets;

    // array of boolean indicating finished machines
    bool finished[num_machines];
    for (int i = 0; i < num_machines; i++)
    {
        finished[i] = false;
    }

    int highest_received[num_machines];
    for (int i = 0; i < num_machines; i++)
    {
        highest_received[i] = 0;
    }

    struct timeval timestamps[buffer_size];
    for (int i = 0; i < buffer_size; i++)
    {
        timerclear(&timestamps[i]);
    }

    // interval in between sending NACK
    struct timeval retransmit_interval;
    retransmit_interval.tv_sec = RETRANSMIT_INTERVAL_SEC;
    retransmit_interval.tv_usec = RETRANSMIT_INTERVAL_USEC;

    int counter = 0;
    int last_delivered_counter = 0;

    // Receive START packet
    bytes_received = recv(sr, received_packet, sizeof(struct packet), 0);
    printf("Receive START pakcet\n");
    free(received_packet);
    recv_dbg_init(loss_rate, machine_index);
    if (bytes_received != sizeof(struct packet))
    {
        printf("Warning: number of bytes in the received pakcet does not equal to size of packet");
    }

    // record starting time of transfer
    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    // initialize created_packets
    int num_created = 0;
    struct packet *data_packet = NULL;
    while (num_created < num_packets && num_created < buffer_size)
    {
        // create new packet
        data_packet = malloc(sizeof(struct packet));
        data_packet->tag = TAG_DATA;
        counter++;
        data_packet->counter = counter;
        data_packet->machine_index = machine_index;
        data_packet->packet_index = num_created + 1;
        data_packet->random_data = (rand() % 999999) + 1;
        created_packets[num_created] = data_packet;
        num_created++;
    }
    for (int i = 0; i < buffer_size / FRACTION_TO_SEND; i++)
    {
        sendto(ss, created_packets[i], sizeof(struct packet), 0,
               (struct sockaddr *)&send_addr, sizeof(send_addr));
        // record current timestamp
        gettimeofday(&timestamps[i], NULL);
    }

    struct packet end_packet;
    end_packet.tag = TAG_END;
    end_packet.machine_index = machine_index;
    // put last packet_index
    end_packet.packet_index = num_packets;
    if (num_created == num_packets)
    {
        sendto(ss, &end_packet, sizeof(struct packet), 0,
               (struct sockaddr *)&send_addr, sizeof(send_addr));
    }
    if (num_packets == 0)
    {
        finished[machine_index - 1] = true;
    }
    struct packet nack_packet;
    nack_packet.tag = TAG_NACK;
    nack_packet.machine_index = machine_index;
    for (int i = 0; i < num_machines; i++)
    {
        nack_packet.payload[i] = -1;
    }

    // file pointer for writing
    FILE *fd;
    char filename[7];
    filename[6] = '\0';
    sprintf(filename, "%d.out", machine_index);
    fd = fopen(filename, "w");

    // contains last delivered counters of machines
    int last_counters[num_machines];
    for (int i = 0; i < num_machines; i++)
    {
        last_counters[i] = -1;
    }

    bool ready_to_end = false;

    for (;;)
    {
        temp_mask = mask;
        timeout.tv_sec = TIMEOUT_SEC;
        timeout.tv_usec = TIMEOUT_USEC;
        num = select(FD_SETSIZE, &temp_mask, NULL, NULL, &timeout);
        if (num > 0)
        {
            if (FD_ISSET(sr, &temp_mask))
            {
                received_packet = malloc(sizeof(struct packet));
                bytes_received = recv_dbg(sr, (char *)received_packet, sizeof(struct packet), 0);

                // packet is lost
                if (bytes_received == 0)
                {
                    free(received_packet);
                    continue;
                }
                if (bytes_received != sizeof(struct packet))
                {
                    printf("Warning: number of bytes in the received pakcet does not equal to size of packet\n");
                }
                if (received_packet->machine_index == machine_index)
                {
                    free(received_packet);
                    // ignore packets sent by my machine
                    continue;
                }

                print_status(acks, start_array_indices, start_packet_indices, end_indices, finished, last_counters, counter, last_delivered_counter, num_created, machine_index, num_machines);
                print_packet(received_packet, num_machines);

                switch (received_packet->tag)
                {
                case TAG_START:
                {
                    printf("Warning: receive START packet in the middle of delivery\n");
                    free(received_packet);
                    break;
                }

                case TAG_ACK:
                {
                }
                case TAG_DATA:
                {

                    if (received_packet->tag == TAG_DATA)
                    {
                        // continue if my machine has finished delivery
                        if (ready_to_end || check_finished_delivery(finished, last_counters, num_machines, machine_index, counter))
                        {
                            // send ack
                            struct packet ack_packet;
                            ack_packet.tag = TAG_ACK;
                            ack_packet.machine_index = machine_index;
                            for (int i = 0; i < num_machines; i++)
                            {
                                if (i + 1 == machine_index)
                                {
                                    ack_packet.payload[i] = acks[i];
                                }
                                else
                                {
                                    ack_packet.payload[i] = start_packet_indices[i] - 1;
                                }
                            }
                            sendto(ss, &ack_packet, sizeof(struct packet), 0,
                                   (struct sockaddr *)&send_addr, sizeof(send_addr));
                            
                            free(received_packet);
                            continue;
                        }

                        // if received packet index not in range
                        if (!(received_packet->packet_index >= start_packet_indices[received_packet->machine_index - 1] && received_packet->packet_index < start_packet_indices[received_packet->machine_index - 1] + buffer_size))
                        {
                            // TODO: send ack/nack
                            free(received_packet);
                            continue;
                        }

                        // insert packet to table
                        int insert_index = convert(received_packet->packet_index, start_packet_indices[received_packet->machine_index - 1], start_array_indices[received_packet->machine_index - 1], buffer_size);
                        // checks if the target spot is empty
                        if (table[received_packet->machine_index - 1][insert_index] == NULL)
                        {
                            table[received_packet->machine_index - 1][insert_index] = received_packet;

                            // send NACK if received packet is not in order
                            if (received_packet->packet_index > highest_received[received_packet->machine_index - 1])
                            {
                                for (int i = highest_received[received_packet->machine_index - 1] + 1;
                                     i < received_packet->packet_index;
                                     i++)
                                {
                                    struct packet nack_packet;
                                    nack_packet.tag = TAG_NACK;
                                    nack_packet.machine_index = machine_index;
                                    for (int i = 0; i < num_machines; i++)
                                    {
                                        nack_packet.payload[i] = -1;
                                    }
                                    nack_packet.payload[received_packet->machine_index - 1] = i;
                                    sendto(ss, &nack_packet, sizeof(struct packet), 0,
                                           (struct sockaddr *)&send_addr, sizeof(send_addr));
                                }
                                highest_received[received_packet->machine_index - 1] = received_packet->packet_index;
                            }
                        }
                        else
                        {
                            free(received_packet);
                            continue;
                        }

                        // adopt the larger counter
                        if (counter < received_packet->counter)
                        {
                            counter = received_packet->counter;
                        }
                    }
                    else if (received_packet->tag == TAG_ACK)
                    {

                        if (ready_to_end || check_acks(acks, num_machines, num_packets))
                        { // to avoid infinite loop
                            free(received_packet);
                            continue;
                        }

                        acks[received_packet->machine_index - 1] = received_packet->payload[machine_index - 1];

                        // check if can slide the window and create new packets
                        int min = acks[0];
                        for (int j = 0; j < num_machines; j++)
                        {
                            if (acks[j] < min)
                            {
                                min = acks[j];
                            }
                        }

                        while (min >= start_packet_indices[machine_index - 1] && num_created < num_packets)
                        {

                            // create new packet
                            data_packet = malloc(sizeof(struct packet));
                            data_packet->tag = TAG_DATA;
                            counter++;
                            data_packet->counter = counter;
                            data_packet->machine_index = machine_index;
                            data_packet->packet_index = num_created + 1;

                            data_packet->random_data = (rand() % 999999) + 1;
                            num_created++;
                            if (created_packets[start_array_indices[machine_index - 1]] != NULL)
                            {
                                free(created_packets[start_array_indices[machine_index - 1]]);
                            }
                            created_packets[start_array_indices[machine_index - 1]] = data_packet;

                            sendto(ss, created_packets[start_array_indices[machine_index - 1]], sizeof(struct packet), 0,
                                   (struct sockaddr *)&send_addr, sizeof(send_addr));

                            if (num_created == num_packets)
                            {
                                sendto(ss, &end_packet, sizeof(struct packet), 0,
                                       (struct sockaddr *)&send_addr, sizeof(send_addr));
                            }

                            // slide window
                            start_array_indices[machine_index - 1] = (start_array_indices[machine_index - 1] + 1) % buffer_size;
                            start_packet_indices[machine_index - 1]++;
                        }

                        // check if ready_to_end after updating ack
                        if (!ready_to_end && check_finished_delivery(finished, last_counters, num_machines, machine_index, counter) && check_acks(acks, num_machines, num_packets))
                        {
                            fclose(fd);
                            ready_to_end = true;

                            printf("=========================\n");
                            printf("       Ready to end\n");
                            printf("=========================\n");

                            struct packet last_counter_packet;
                            last_counter_packet.tag = TAG_COUNTER;
                            last_counter_packet.machine_index = machine_index;
                            last_counters[machine_index - 1] = counter;
                            for (int i = 0; i < num_machines; i++)
                            {
                                last_counter_packet.payload[i] = last_counters[i];
                            }
                            sendto(ss, &last_counter_packet, sizeof(struct packet), 0,
                                   (struct sockaddr *)&send_addr, sizeof(send_addr));
                        }
                    }

                    if (check_finished_delivery(finished, last_counters, num_machines, machine_index, counter))
                    {
                        free(received_packet);
                        continue;
                    }

                    if (received_packet->tag == TAG_ACK)
                    {
                        free(received_packet);
                    }

                    // try to deliver packets
                    /*
                        We define is_full to be true when the all packets with the next delivered 
                        counter has arrived or has been generated 
                    */
                    bool is_full = true;
                    while (is_full)
                    {
                        bool deliverable[num_machines];
                        // initialize all deliverables to false
                        for (int i = 0; i < num_machines; i++)
                        {
                            deliverable[i] = false;
                        }

                        // update deliverables and check if a row is full
                        for (int i = 0; i < num_machines; i++)
                        {
                            if (finished[i])
                            { // skips this machine if I have finished delivering all its packets
                                nack_packet.payload[i] = -1;
                                continue;
                            }
                            if (i != machine_index - 1)
                            { // other machine case
                                if (table[i][start_array_indices[i]] != NULL)
                                { // next packet is in the table
                                    // deliverable if next packet has a counter that should be delivered next
                                    deliverable[i] = (table[i][start_array_indices[i]]->counter == last_delivered_counter + 1);
                                    nack_packet.payload[i] = -1; // packet exits, don't nack
                                }
                                else
                                { // packet hasn't arrived yet
                                    deliverable[i] = false;
                                    nack_packet.payload[i] = start_packet_indices[i]; // missing packet, nack
                                    is_full = false;
                                }
                            }
                            else
                            { // my machine case
                                if (acks[i] + 1 > num_created)
                                { // hasn't been created yet
                                    is_full = false;
                                    deliverable[i] = false;
                                }
                                else
                                {
                                    int index = convert(acks[i] + 1, start_packet_indices[i], start_array_indices[i], buffer_size);
                                    deliverable[i] = (created_packets[index]->counter == last_delivered_counter + 1);
                                }
                            }
                        }

                        if (is_full)
                        { // can deliver
                            last_delivered_counter++;
                            for (int i = 0; i < num_machines; i++)
                            {
                                if (finished[i])
                                { // skips this machine if I have finished delivering all its packets
                                    continue;
                                }

                                if (!deliverable[i])
                                {
                                    continue;
                                }

                                if (i + 1 == machine_index)
                                { // my machine case
                                    acks[i]++;
                                    int index = convert(acks[i], start_packet_indices[i], start_array_indices[i], buffer_size);
                                    fprintf(fd, "%2d, %8d, %8d\n", machine_index, acks[i], created_packets[index]->random_data);

                                    // if delivered last packet, mark as finished
                                    if (acks[i] == num_packets)
                                    {
                                        finished[i] = true;
                                        nack_packet.payload[i] = -1;
                                        continue;
                                    }

                                    int min = acks[0];
                                    for (int j = 0; j < num_machines; j++)
                                    {
                                        if (acks[j] < min)
                                        {
                                            min = acks[j];
                                        }
                                    }

                                    while (min >= start_packet_indices[i] && num_created < num_packets)
                                    {
                                        // create new packet
                                        data_packet = malloc(sizeof(struct packet));
                                        data_packet->tag = TAG_DATA;
                                        counter++;
                                        data_packet->counter = counter;
                                        data_packet->machine_index = machine_index;
                                        data_packet->packet_index = num_created + 1;
                                        num_created++;
                                        data_packet->random_data = (rand() % 999999) + 1;

                                        if (created_packets[start_array_indices[machine_index - 1]] != NULL)
                                        {
                                            free(created_packets[start_array_indices[machine_index - 1]]);
                                        }
                                        created_packets[start_array_indices[machine_index - 1]] = data_packet;

                                        sendto(ss, created_packets[start_array_indices[i]], sizeof(struct packet), 0,
                                               (struct sockaddr *)&send_addr, sizeof(send_addr));

                                        // record first time send
                                        gettimeofday(&timestamps[start_array_indices[machine_index - 1]], NULL);

                                        if (num_created == num_packets)
                                        {
                                            sendto(ss, &end_packet, sizeof(struct packet), 0,
                                                   (struct sockaddr *)&send_addr, sizeof(send_addr));
                                        }

                                        // slide window
                                        start_array_indices[i] = (start_array_indices[i] + 1) % buffer_size;
                                        start_packet_indices[i]++;
                                    }
                                }
                                else
                                { // other machine case
                                    if (i + 1 != table[i][start_array_indices[i]]->machine_index)
                                    {
                                        printf("Warning: variable i doesn't match with the machine index in the table\n");
                                    }
                                    if (start_packet_indices[i] != table[i][start_array_indices[i]]->packet_index)
                                    {
                                        printf("Warning: packet index doesn't match\n");
                                    }

                                    fprintf(fd, "%2d, %8d, %8d\n", i + 1, start_packet_indices[i], table[i][start_array_indices[i]]->random_data);

                                    // discard delivered packet in table
                                    free(table[i][start_array_indices[i]]);
                                    table[i][start_array_indices[i]] = NULL;

                                    // slide window for delivering
                                    start_array_indices[i] = (start_array_indices[i] + 1) % buffer_size;
                                    start_packet_indices[i]++;

                                    // check if the machine has finished. update the finished array if yes.
                                    if (end_indices[i] != -1 && start_packet_indices[i] > end_indices[i])
                                    { // finished
                                        finished[i] = true;
                                        nack_packet.payload[i] = -1;
                                    }

                                } // end of if

                                if (last_delivered_counter % (buffer_size / FRACTION_DELIVERY_GAP) == 0 || check_finished_delivery(finished, last_counters, num_machines, machine_index, counter))
                                {
                                    // send ack
                                    struct packet ack_packet;
                                    ack_packet.tag = TAG_ACK;
                                    ack_packet.machine_index = machine_index;
                                    for (int i = 0; i < num_machines; i++)
                                    {
                                        if (i + 1 == machine_index)
                                        {
                                            ack_packet.payload[i] = acks[i];
                                        }
                                        else
                                        {
                                            ack_packet.payload[i] = start_packet_indices[i] - 1;
                                        }
                                    }
                                    sendto(ss, &ack_packet, sizeof(struct packet), 0,
                                           (struct sockaddr *)&send_addr, sizeof(send_addr));
                                }

                            } // end of deliver for loop

                            // check if ready_to_end after each delivery
                            if (!ready_to_end && check_finished_delivery(finished, last_counters, num_machines, machine_index, counter) && check_acks(acks, num_machines, num_packets))
                            {
                                fclose(fd);
                                ready_to_end = true;

                                printf("=========================\n");
                                printf("       Ready to end\n");
                                printf("=========================\n");

                                struct packet last_counter_packet;
                                last_counter_packet.tag = TAG_COUNTER;
                                last_counter_packet.machine_index = machine_index;
                                last_counters[machine_index - 1] = counter;
                                for (int i = 0; i < num_machines; i++)
                                {
                                    last_counter_packet.payload[i] = last_counters[i];
                                }
                                sendto(ss, &last_counter_packet, sizeof(struct packet), 0,
                                       (struct sockaddr *)&send_addr, sizeof(send_addr));
                            }

                            // if finish delivery, break out of while loop
                            if (check_finished_delivery(finished, last_counters, num_machines, machine_index, counter))
                            {
                                break;
                            }
                        }
                        else
                        { // not full, we have missing packets, send nack
                            sendto(ss, &nack_packet, sizeof(struct packet), 0,
                                   (struct sockaddr *)&send_addr, sizeof(send_addr));
                        }
                    } // end of while loop
                    break;
                }

                case TAG_NACK:
                {

                    // only respond to NACK if asked for current machine's packet
                    int i = machine_index - 1;
                    int requested_packet_index = received_packet->payload[i];
                    if (requested_packet_index != -1)
                    {
                        // if received packet index is larger than num_packets, send END packet
                        if (requested_packet_index > num_packets)
                        {
                            sendto(ss, &end_packet, sizeof(struct packet), 0,
                                   (struct sockaddr *)&send_addr, sizeof(send_addr));
                        }

                        // if received packet index not in range
                        if (!(requested_packet_index >= start_packet_indices[i] && requested_packet_index < start_packet_indices[i] + buffer_size))
                        {
                            continue;
                        }

                        else
                        {
                            int index = convert(requested_packet_index, start_packet_indices[machine_index - 1], start_array_indices[machine_index - 1], buffer_size);

                            struct timeval now;
                            struct timeval interval;
                            gettimeofday(&now, NULL);
                            timersub(&now, &timestamps[index], &interval);
                            if (!timerisset(&timestamps[index]) || !timercmp(&interval, &retransmit_interval, <))
                            {
                                sendto(ss, created_packets[index], sizeof(struct packet), 0,
                                       (struct sockaddr *)&send_addr, sizeof(send_addr));
                                gettimeofday(&timestamps[index], NULL);
                            }
                        }
                    }

                    free(received_packet);
                    break;
                }

                case TAG_END:
                {
                    end_indices[received_packet->machine_index - 1] = received_packet->packet_index;

                    // update finished array if possible
                    if (start_packet_indices[received_packet->machine_index - 1] > end_indices[received_packet->machine_index - 1])
                    {
                        finished[received_packet->machine_index - 1] = true;
                        nack_packet.payload[received_packet->machine_index - 1] = -1;
                        if (!ready_to_end && check_finished_delivery(finished, last_counters, num_machines, machine_index, counter) && check_acks(acks, num_machines, num_packets))
                        {
                            fclose(fd);
                            ready_to_end = true;

                            printf("=========================\n");
                            printf("       Ready to end\n");
                            printf("=========================\n");

                            struct packet last_counter_packet;
                            last_counter_packet.tag = TAG_COUNTER;
                            last_counter_packet.machine_index = machine_index;
                            last_counters[machine_index - 1] = counter;
                            for (int i = 0; i < num_machines; i++)
                            {
                                last_counter_packet.payload[i] = last_counters[i];
                            }
                            sendto(ss, &last_counter_packet, sizeof(struct packet), 0,
                                   (struct sockaddr *)&send_addr, sizeof(send_addr));
                        }
                    }
                    free(received_packet);
                    break;
                }

                case TAG_COUNTER:
                {
                    bool new_info = false;
                    // update last_counters based on counter packet
                    for (int i = 0; i < num_machines; i++)
                    {
                        if (received_packet->payload[i] != -1 && last_counters[i] == -1)
                        {
                            new_info = true;
                            last_counters[i] = received_packet->payload[i];
                        }
                    }

                    // send new counter packet
                    struct packet last_counter_packet;
                    last_counter_packet.tag = TAG_COUNTER;
                    last_counter_packet.machine_index = machine_index;
                    for (int i = 0; i < num_machines; i++)
                    {
                        last_counter_packet.payload[i] = last_counters[i];
                    }

                    if (new_info)
                    {
                        sendto(ss, &last_counter_packet, sizeof(struct packet), 0,
                               (struct sockaddr *)&send_addr, sizeof(send_addr));
                    }

                    bool all_received = true;
                    for (int i = 0; i < num_machines; i++)
                    {
                        all_received = all_received && last_counters[i] == last_delivered_counter;
                    }

                    if (all_received)
                    {
                        for (int i = 0; i < NUM_EXIT_SIGNALS; i++)
                        {
                            sendto(ss, &last_counter_packet, sizeof(struct packet), 0,
                                   (struct sockaddr *)&send_addr, sizeof(send_addr));
                        }

                        printf("=========================\n");
                        printf("           EXIT\n");
                        printf("=========================\n");

                        // record starting time of transfer
                        struct timeval end_time;
                        gettimeofday(&end_time, NULL);
                        struct timeval diff_time = diffTime(end_time, start_time);
                        double seconds = diff_time.tv_sec + ((double)diff_time.tv_usec) / 1000000;
                        printf("Trasmission time is %.2f seconds\n", seconds);

                        free(received_packet);
                        for (int i = 0; i < buffer_size; i++)
                        {
                            if (created_packets[i] != NULL)
                            {
                                free(created_packets[i]);
                            }
                        }

                        /*for (int i = 0; i < num_machines; i++) {
                            for (int j=0; j < buffer_size; j++) {
                                if(table[i][j] != NULL) {
                                    printf("Warning: table is not freed!\n");
                                }
                            }
                        }*/

                        exit(0);
                    }
                    free(received_packet);
                }
                }
            }
        }
        else
        {
            // timeout
            // send ack
            //printf("TIMEOUT!\n");

            if (!ready_to_end)
            {
                if (!check_finished_delivery(finished, last_counters, num_machines, machine_index, counter))
                {
                    struct packet ack_packet;
                    ack_packet.tag = TAG_ACK;
                    ack_packet.machine_index = machine_index;
                    for (int i = 0; i < num_machines; i++)
                    {
                        ack_packet.payload[i] = start_packet_indices[i] - 1;
                    }
                    sendto(ss, &ack_packet, sizeof(struct packet), 0,
                           (struct sockaddr *)&send_addr, sizeof(send_addr));

                    sendto(ss, &nack_packet, sizeof(struct packet), 0,
                           (struct sockaddr *)&send_addr, sizeof(send_addr));
                }
                else
                { // I finished delivery
                    sendto(ss, &end_packet, sizeof(struct packet), 0,
                           (struct sockaddr *)&send_addr, sizeof(send_addr));
                }

                if (!check_acks(acks, num_machines, num_packets))
                {
                    int index = convert(num_created, start_packet_indices[machine_index - 1], start_array_indices[machine_index - 1], buffer_size);
                    // send highest generated packet
                    sendto(ss, created_packets[index], sizeof(struct packet), 0,
                           (struct sockaddr *)&send_addr, sizeof(send_addr));
                }
            }
            else
            {
                struct packet last_counter_packet;
                last_counter_packet.tag = TAG_COUNTER;
                last_counter_packet.machine_index = machine_index;
                for (int i = 0; i < num_machines; i++)
                {
                    last_counter_packet.payload[i] = last_counters[i];
                }
                sendto(ss, &last_counter_packet, sizeof(struct packet), 0,
                       (struct sockaddr *)&send_addr, sizeof(send_addr));
            }
        }
    }

    return 0;
}