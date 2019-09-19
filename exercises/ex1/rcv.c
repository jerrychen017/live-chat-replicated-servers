#include "net_include.h"
#include "packet.h"
#include "sendto_dbg.h"
#include "tag.h"

#define NACK_INTERVAL 2

unsigned int convert(unsigned int sequence, unsigned int start_sequence, unsigned int start_index);

int main(int argc, char** argv) {
    
    // argc error checking
    if (argc != 2) {
        printf("Usage: rcv <loss_rate_percent>\n");
        exit(0);
    }

    int loss_rate_percent = atoi(argv[1]);

    // if 2nd command is not valid number
    if (loss_rate_percent == 0 && strcmp(argv[1], "0") != 0) {
        perror("Error: second command should be an integer in [0, 100]\n");
        exit(0);
    }

    if (loss_rate_percent < 0 || loss_rate_percent > 100) {
        printf("Warning: loss_rate_percent should be within range [0, 100]\n");
        printf("         if < 0, set to 0; if > 100, set to 100\n");
    }

    sendto_dbg_init(loss_rate_percent);

    bool busy = false;

    // create a socket for receiving packets
    int socket_rcv = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_rcv < 0) {
        perror("Rcv: cannot create a socket for receiving");
        exit(1);
    }

    struct sockaddr_in sockaddr_rcv;
    sockaddr_rcv.sin_family = AF_INET;
    sockaddr_rcv.sin_addr.s_addr = INADDR_ANY;
    sockaddr_rcv.sin_port = htons(PORT);

    // bind the socket with rcv address
    if (bind(socket_rcv, (struct sockaddr*)&sockaddr_rcv, sizeof(sockaddr_rcv)) < 0) {
        perror("Rcv: cannot bind socket with rcv address");
        exit(1);
    }

    // create a socket for sending packet_mess
    int socket_sent = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_sent < 0) {
        perror("Rcv: cannot create a socket for sending");
        exit(1);
    }

    fd_set read_mask;
    fd_set mask;
    FD_ZERO(&mask);
    FD_SET(socket_rcv, &mask);

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

    for (;;) {
        read_mask = mask;
        idle_interval.tv_sec = 10;
        idle_interval.tv_usec = 0;
        int num = select(FD_SETSIZE, &read_mask, NULL, NULL, &idle_interval);
        if (num > 0) {
            if (FD_ISSET(socket_rcv, &read_mask)) {
                sockaddr_ncp_len = sizeof(sockaddr_ncp);
                // receive packet form ncp and store its address
                recvfrom(socket_rcv, &packet_received, sizeof(struct packet), 0,
                        (struct sockaddr*) &sockaddr_ncp, &sockaddr_ncp_len);
                int ncp_ip = sockaddr_ncp.sin_addr.s_addr;
                
                // if sender is NOT current client, assume it's previous client
                if (busy && memcmp(&sockaddr_ncp, &sockaddr_client, sockaddr_ncp_len) != 0) {
                    // send special finish tag
                    packet_sent.tag = RCV_END;
                    sendto_dbg(socket_sent, (char *) &packet_sent, sizeof(struct packet), 0,
                            (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                    continue;
                }


                switch (packet_received.tag) {

                    // if sender wants to start transferring
                    case NCP_FILENAME:
                    {
                        if (busy) {
                            // send packet to notify busy
                            packet_sent.tag = RCV_BUSY;
                            printf("Sender (%d.%d.%d.%d) wants to connect, but busy\n",
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

                            // store the client address
                            memcpy(&sockaddr_client, &sockaddr_ncp, sockaddr_ncp_len);

                            // send packet to start transfer
                            packet_sent.tag = RCV_START;
                            printf("Establish connection with sender (%d.%d.%d.%d)\n",
                                    (htonl(ncp_ip) & 0xff000000) >> 24,
                                    (htonl(ncp_ip) & 0x00ff0000) >> 16,
                                    (htonl(ncp_ip) & 0x0000ff00) >> 8,
                                    (htonl(ncp_ip) & 0x000000ff));
                        }
                        sendto_dbg(socket_sent, (char *)&packet_sent, sizeof(struct packet), 0, 
                                (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                        break;
                    }


                    // if receive LAST packet
                    case NCP_LAST:
                    {
                        // record the last sequence
                        last_sequence = packet_received.sequence;
                    }

                    // if sender is transferring
                    case NCP_FILE:
                    {
                        printf("Received a packet with file\n");
                        unsigned int index = convert(packet_received.sequence, start_sequence, start_index);
                        // put buf in the corresponding spot in window
                        
                        // error check on file size
                        if (packet_received.bytes != BUF_SIZE) {
                            printf("Warning: packet contains less than BUF_SIZE bytes\n");
                        }
                        
                        memcpy(window[index], packet_received.file, packet_received.bytes);
                        // mark cell as occupied
                        occupied[index] = true;

                        // find the first gap in the window                        
                        unsigned int cur;
                        for (cur = start_sequence; cur < start_sequence + WINDOW_SIZE; cur++) {
                            index = convert(cur, start_sequence, start_index);
                            if (!occupied[index]) {
                                break;
                            }
                        }
                        unsigned int gap = cur;
                        
                        // write to file
                        for (cur = start_sequence; cur < gap; cur++) {
                            int bytes_written = fwrite(window[convert(cur, start_sequence, start_index)],
                                    1, BUF_SIZE, fw);
                            // error checking on bytes written
                            if (bytes_written != BUF_SIZE) {
                                printf("Warning: write to file less than BUF_SIZE bytes\n");
                            }
                            // clear the timestamp
                            timerclear(&timestamps[convert(cur, start_sequence, start_index)]);
                        }
                        
                        // put ACK in the return packet
                        packet_sent.tag = RCP_ACK;

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
                            for (cur = start_sequence + WINDOW_SIZE - 1; cur >= gap; cur--) {
                                if (occupied[convert(cur, start_sequence, start_index)]) {
                                    break;
                                }
                            }
                            unsigned int last_received = cur;
                            
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
                        
                        // mark written cells as not occupied
                        for (cur = start_sequence; cur < gap; cur++) {
                            occupied[convert(cur, start_sequence, start_index)] = false;
                        }

                        // slide window
                        start_sequence = gap;
                        start_index = (start_index + gap - start_sequence) % WINDOW_SIZE;


                        // if haven't received last packet
                        if (last_sequence == UINT_MAX || start_sequence != last_sequence + 1) {
                            sendto_dbg(socket_sent, (char *) &packet_sent, sizeof(struct packet), 0,
                                    (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                        } else {
                            // send special finish tag
                            packet_sent.tag = RCV_END;
                            sendto_dbg(socket_sent, (char *) &packet_sent, sizeof(struct packet), 0,
                                    (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));

                            // clean up
                            fclose(fw);
                            busy = false;
                            start_sequence = 0;
                            start_index = 0;
                            memset(occupied, 0, WINDOW_SIZE * sizeof(bool));
                            for (int i = 0; i < WINDOW_SIZE; i++) {
                                timerclear(&timestamps[i]);
                            }

                            printf("Finish trasnferring file with sender (%d.%d.%d.%d)\n",
                                    (htonl(ncp_ip) & 0xff000000) >> 24,
                                    (htonl(ncp_ip) & 0x00ff0000) >> 16,
                                    (htonl(ncp_ip) & 0x0000ff00) >> 8,
                                    (htonl(ncp_ip) & 0x000000ff));
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
