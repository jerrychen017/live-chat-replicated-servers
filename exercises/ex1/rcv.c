#include "net_include.h"
#include "packet.h"
#include "sendto_dbg.h"
#include "tag.h"

unsigned int convert(unsigned int sequence, unsigned int start_sequence, unsigned int start_index);
void print_sent_packet(struct packet_mess* packet_sent);
void print_received_packet(struct packet* packet_received);

int main(int argc, char** argv) {
    
    // argc error checking
    if (argc != 2) {
        printf("Usage: rcv <loss_rate_percent>\n");
        exit(0);
    }

    int loss_rate_percent = atoi(argv[1]);

    // if 2nd command is not valid number
    if (loss_rate_percent == 0 && strcmp(argv[1], "0") != 0) {
        printf("Error: second command should be an integer in [0, 100]\n");
        exit(0);
    }

    if (loss_rate_percent < 0 || loss_rate_percent > 100) {
        printf("Warning: loss_rate_percent should be within range [0, 100]\n");
        printf("         if < 0, set to 0; if > 100, set to 100\n");
    }

    sendto_dbg_init(loss_rate_percent);

    bool busy = false;

    // create a socket both for sending and receiving
    int sk = socket(AF_INET, SOCK_DGRAM, 0);
    if (sk < 0) {
        perror("Rcv: cannot create a socket for receiving");
        exit(1);
    }

    struct sockaddr_in sockaddr_rcv;
    sockaddr_rcv.sin_family = AF_INET;
    sockaddr_rcv.sin_addr.s_addr = INADDR_ANY;
    sockaddr_rcv.sin_port = htons(PORT);

    // bind the socket with PORT
    if (bind(sk, (struct sockaddr*)&sockaddr_rcv, sizeof(sockaddr_rcv)) < 0) {
        perror("Rcv: cannot bind socket with rcv address");
        exit(1);
    }

    fd_set read_mask;
    fd_set mask;
    FD_ZERO(&mask);
    FD_SET(sk, &mask);

    // wait interval for connection requests
    struct timeval idle_interval;
    // interval in between sending NACK
    struct timeval nack_interval;
    nack_interval.tv_sec = NACK_INTERVAL;
    nack_interval.tv_usec = 0;

    // socket address of received packet
    struct sockaddr_in sockaddr_ncp;
    socklen_t sockaddr_ncp_len;

    // socket address of current client
    struct sockaddr_in sockaddr_client;

    // packet received from sender
    struct packet packet_received;
    // packet delivered to sender
    struct packet_mess packet_sent;

    // window to store files
    struct packet window[WINDOW_SIZE][BUF_SIZE];
    // sequence which the first cell of window corresponds to
    unsigned int start_sequence = 0;
    // start index of the array
    unsigned int start_index = 0;

    // bool array to indicate if cell is filled
    bool occupied[WINDOW_SIZE];
    memset(occupied, 0, WINDOW_SIZE * sizeof(bool));
    
    // timestamp array for NACK (initialized to zero)
    struct timeval timestamps[WINDOW_SIZE];
    for (int i = 0; i < WINDOW_SIZE; i++) {
        timerclear(&timestamps[i]);
    }
    
    // file pointer for writing
    FILE *fw;

    // sequence of last packet
    unsigned int last_sequence = UINT_MAX;
    // valid bytes in last packet
    unsigned int last_packet_bytes = 0;

    // number of bytes written
    unsigned int bytes = 0;
    // clock when receive the first packet
    clock_t start_clock;
    // clock when receive last 100 Mbytes
    clock_t last_clock;
    
    for (;;) {
        read_mask = mask;
        idle_interval.tv_sec = 10;
        idle_interval.tv_usec = 0;
        int num = select(FD_SETSIZE, &read_mask, NULL, NULL, &idle_interval);
        if (num > 0) {
            if (FD_ISSET(sk, &read_mask)) {
                sockaddr_ncp_len = sizeof(sockaddr_ncp);
                // receive packet form ncp and store its address
                recvfrom(sk, &packet_received, sizeof(struct packet), 0,
                        (struct sockaddr*) &sockaddr_ncp, &sockaddr_ncp_len);
                int ncp_ip = sockaddr_ncp.sin_addr.s_addr;
                
                // if not NCP_FILENAME, and sender is NOT current client
                if (packet_received.tag != NCP_FILENAME
                        && !(busy && memcmp(&sockaddr_ncp, &sockaddr_client, sockaddr_ncp_len) == 0)){
                    // assume sender is previous client
                    // send special finish tag
                    packet_sent.tag = RCV_END;
                    sendto_dbg(sk, (char *) &packet_sent, sizeof(struct packet_mess), 0,
                            (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                    
                    printf("Sender (%d.%d.%d.%d) is not current client, or does not send proper NCP_FILENAME tag, respond with RCV_END packet\n",
                            (htonl(ncp_ip) & 0xff000000) >> 24,
                            (htonl(ncp_ip) & 0x00ff0000) >> 16,
                            (htonl(ncp_ip) & 0x0000ff00) >> 8,
                            (htonl(ncp_ip) & 0x000000ff));

                    continue;
                }


                switch (packet_received.tag) {

                    // if sender wants to start transferring
                    case NCP_FILENAME:
                    {
                        // if it is NOT current client, send packet to notify busy
                        if (busy && memcmp(&sockaddr_ncp, &sockaddr_client, sockaddr_ncp_len) != 0) {
                            packet_sent.tag = RCV_BUSY;
                            printf("Notice: Sender (%d.%d.%d.%d) wants to connect, but busy\n",
                                    (htonl(ncp_ip) & 0xff000000) >> 24,
                                    (htonl(ncp_ip) & 0x00ff0000) >> 16,
                                    (htonl(ncp_ip) & 0x0000ff00) >> 8,
                                    (htonl(ncp_ip) & 0x000000ff));
                        } else {
                            // open or create file for writing
                            char filename[packet_received.bytes + 1];
                            memcpy(filename, packet_received.file, packet_received.bytes);
                            filename[packet_received.bytes] = '\0';
                            fw = fopen(filename, "w");
                            if (fw == NULL) {
                                perror("Rcv: cannot open or create file for writing");
                                exit(1);
                            }

                            busy = true;

                            // store the client address
                            memcpy(&sockaddr_client, &sockaddr_ncp, sockaddr_ncp_len);

                            // send packet to start transfer
                            packet_sent.tag = RCV_START;
                            printf("\n");
                            printf("----------------START-----------------\n");
                            printf("Establish connection with sender (%d.%d.%d.%d)\n",
                                    (htonl(ncp_ip) & 0xff000000) >> 24,
                                    (htonl(ncp_ip) & 0x00ff0000) >> 16,
                                    (htonl(ncp_ip) & 0x0000ff00) >> 8,
                                    (htonl(ncp_ip) & 0x000000ff));
                            printf("Start transferring file %s\n", filename);

                            // start clock to record time for transfer
                            start_clock = clock();
                            last_clock = clock();
                        }
                        sendto_dbg(sk, (char *)&packet_sent, sizeof(struct packet_mess), 0, 
                                (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                        break;
                    }

                    // if receive LAST packet
                    case NCP_LAST:
                    {
                        // record the last sequence
                        last_sequence = packet_received.sequence;
                        printf("lastlast sequence is %d\n", last_sequence);
                        last_packet_bytes = packet_received.bytes;
                        printf("last packet size is %d\n", last_packet_bytes);
                    }

                    // if sender is transferring
                    case NCP_FILE:
                    {
                        printf("----------------START-----------------\n");
                        print_received_packet(&packet_received);
                        
                        // if received packets we have acknowledged
                        // we have acknowledged one window, but ACK packet is lost
                        // send back ACK again
                        if (packet_received.sequence < start_sequence) {
                            packet_sent.tag = RCV_ACK;
                            packet_sent.ack = start_sequence - 1;
                            packet_sent.nums_nack = 0;
                            sendto_dbg(sk, (char *) &packet_sent, sizeof(struct packet_mess), 0,
                                    (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                            break;
                        }

                        unsigned int index = convert(packet_received.sequence, start_sequence, start_index);
                        memcpy(window[index], packet_received.file, packet_received.bytes);
                        
                        // mark cell as occupied
                        occupied[index] = true;

                        printf("start sequence is %d\n", start_sequence);
                        printf("start index is %d\n", start_index);
                        printf("occupied array:\n");
                        for (int i = 0; i < WINDOW_SIZE; i++) {
                            printf("index %d: %d ", i, occupied[i]);
                        }
                        // find the first gap in the window                        
                        unsigned int cur;
                        for (cur = start_sequence; cur < start_sequence + WINDOW_SIZE; cur++) {
                            index = convert(cur, start_sequence, start_index);
                            if (!occupied[index]) {
                                break;
                            }
                        }
                        unsigned int gap = cur;
                        printf("gap is %d\n", gap);
                        // write to file
                        for (cur = start_sequence; cur < gap; cur++) {
                            unsigned int old_bytes = bytes;
                            // if writing the last packet to file
                            if (last_sequence != UINT_MAX && cur == last_sequence) {
                                int bytes_written = fwrite(window[convert(cur,
                                            start_sequence, start_index)], 1, last_packet_bytes, fw);
                                // error checking on bytes written
                                if (bytes_written != last_packet_bytes) {
                                }
                                bytes += bytes_written;
                            } else {
                                int bytes_written = fwrite(window[convert(cur,
                                            start_sequence, start_index)], 1, BUF_SIZE, fw);
                                // error checking on bytes written
                                if (bytes_written != BUF_SIZE) {
                                    printf("Warning: write to file less than %lu bytes\n", BUF_SIZE);
                                }
                                bytes += bytes_written;
                            }
                            // clear the timestamp
                            timerclear(&timestamps[convert(cur, start_sequence, start_index)]);

                            // report statistics every 100 MBytes
                            if (old_bytes / 100000000 != bytes / 100000000) {
                                double seconds = ((double) (clock() - last_clock)) / CLOCKS_PER_SEC;
                                printf("Report: total amount of data transferred is %u Mbytes\n", bytes / (1024 * 1024));
                                printf("        average transfer rate of the last 100 Mbytes received is %.2f Mbits/sec\n", (double) (100) * 8 / seconds);
                                last_clock = clock();
                            }
                        }
                        
                        // mark written cells as not occupied
                        for (cur = start_sequence; cur < gap; cur++) {
                            occupied[convert(cur, start_sequence, start_index)] = false;
                        }

                        // put ACK in the return packet
                        packet_sent.tag = RCV_ACK;

                        // if the first packet is lost, use MAX UNSIGNED INT as ack 
                        if (gap == 0) {
                            packet_sent.ack = UINT_MAX;
                        } else {
                            packet_sent.ack = gap - 1;
                        }

                        // determine if there is NACK
                        // if window is full
                        if (gap == start_sequence + WINDOW_SIZE) {
                            packet_sent.nums_nack = 0;
                        } else {
                            // find the last received packet in window
                            // if have NOT receive the last packet
                            if (last_sequence == UINT_MAX) {
                                for (cur = start_sequence + WINDOW_SIZE - 1; cur >= gap; cur--) {
                                    if (occupied[convert(cur, start_sequence, start_index)]) {
                                        break;
                                    }
                                }
                            } else {
                                cur = last_sequence;
                            }
                            unsigned int last_received = cur;
                           
                            printf("last received packet sequence is %d\n", last_received); 
                            // if no received packet after gap
                            if (last_received == gap - 1) {
                                packet_sent.nums_nack = 0;
                            } else {
                                int counter = 0;

                                struct timeval now;
                                struct timeval interval;

                                // find gaps in between
                                for (cur = gap; cur < last_received; cur++) {
                                    index = convert(cur, start_sequence, start_index);
                                    if (!occupied[index]) {
                                        // if no timestamp, first NACK, include in packet
                                        if (!timerisset(&timestamps[index])) {
                                            packet_sent.nack[counter] = cur;
                                            counter++;
                                            // record current timestamp
                                            gettimeofday(&now, NULL);
                                            memcpy(&timestamps[index], &now, sizeof(struct timeval));
                                        } else {
                                            gettimeofday(&now, NULL);
                                            timersub(&now, &timestamps[index], &interval);
                                            // if interval is large, include in NACK
                                            if (!timercmp(&interval, &nack_interval, <)) {
                                                packet_sent.nack[counter] = cur;
                                                counter++;
                                                // record current timestamp
                                                gettimeofday(&now, NULL);
                                                memcpy(&timestamps[index], &now, sizeof(struct timeval));
                                            }
                                        }

                                        // if number of nack exceeds the limit
                                        if (counter >= NACK_SIZE) {
                                            break;
                                        }
                                    }
                                }

                                packet_sent.nums_nack = counter;
                            }

                        }
                        
                        // slide window
                        start_index = (start_index + gap - start_sequence) % WINDOW_SIZE;
                        start_sequence = gap;

                        printf("start sequence is %d\n", start_sequence);
                        printf("start index is %d\n", start_index);
                        // if haven't received last packet
                        if (last_sequence == UINT_MAX || start_sequence != last_sequence + 1) {
                            print_sent_packet(&packet_sent);
                            sendto_dbg(sk, (char *) &packet_sent, sizeof(struct packet_mess), 0,
                                    (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                            printf("-----------------END------------------\n\n");
                        } else {
                            // send special finish tag
                            packet_sent.tag = RCV_END;
                            sendto_dbg(sk, (char *) &packet_sent, sizeof(struct packet_mess), 0,
                                    (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));

                            printf("Finish trasnferring file with sender (%d.%d.%d.%d)\n",
                                    (htonl(ncp_ip) & 0xff000000) >> 24,
                                    (htonl(ncp_ip) & 0x00ff0000) >> 16,
                                    (htonl(ncp_ip) & 0x0000ff00) >> 8,
                                    (htonl(ncp_ip) & 0x000000ff));

                            // TODO: Error check on start_clock
                            double seconds = ((double) (clock() - start_clock)) / CLOCKS_PER_SEC;
                            printf("Report: Size of file transferred is %d bytes\n", bytes);
                            printf("Report: Size of file transferred is %.2f Mbytes\n", (double) bytes / (1024 * 1024));
                            printf("        Amount of time spent is %.2f seconds\n", seconds);
                            printf("        Average rate is %.2f Mbits/sec\n",
                                    (double) bytes / (1024 * 1024) * 8 / seconds);
                            printf("-----------------END------------------\n");

                            // clean up
                            fclose(fw);
                            busy = false;
                            start_sequence = 0;
                            start_index = 0;
                            memset(occupied, 0, WINDOW_SIZE * sizeof(bool));
                            for (int i = 0; i < WINDOW_SIZE; i++) {
                                timerclear(&timestamps[i]);
                            }
                            last_sequence = UINT_MAX;
                            bytes = 0;
                        }

                        break;
                    }
                }
            }
        } else {
            printf(".");
            fflush(0);
            
        }
    }
    return 0;
}

unsigned int convert(unsigned int sequence, unsigned int start_sequence,
                     unsigned int start_index) {
  return (sequence - start_sequence + start_index) % WINDOW_SIZE;
}

void print_sent_packet(struct packet_mess* packet_sent) {

    printf("Sent packet with ack %d, nums_nack %d, nacks: ", packet_sent->ack, packet_sent->nums_nack);
    for (int i = 0; i < packet_sent->nums_nack; i++) {
        printf("%d ", packet_sent->nack[i]);
    }
    printf("\n");
}

void print_received_packet(struct packet* packet_received) {
    printf("Receive packet with sequence %d, bytes %d\n", packet_received->sequence, packet_received->bytes);
}
