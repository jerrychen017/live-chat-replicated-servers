#include "net_include.h"
#include "sendto_dbg.h"
#include "packet.h"

unsigned int convert(unsigned int sequence, unsigned int start_sequence, unsigned int start_index);

int main(int argc, char** argv) {
    
    // argc erro checking
    if (argc != 3) {
        printf("Usage: rcv <loss_rate_percent>");
        exit(0);
    }

    int loss_rate_percent = atoi(argv[2]);
    if (loss_rate_percent < 0 || loss_rate_percent > 100) {
        printf("Warning: loss_rate_percent should be within range [0, 100]");
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
    if (bind(socket_rcv, (struct sockaddr*) &sockaddr_rcv, sizeof(sockaddr_rcv)) < 0) {
        perror("Rcv: cannot bind socket with rcv address");
        exit(1);
    }

    int socket_sent = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_sent < 0) {
        perror("Rcv: cannot create a socket for sending");
        exit(1);
    }

    fd_set read_mask;
    fd_set mask;
    FD_ZERO(&mask);
    FD_SET(socket_rcv, &mask);

    struct timeval idle_interval;

    struct sockaddr_in sockaddr_ncp;
    socklen_t sockaddr_ncp_len;

    struct packet packet_received;
    struct packet_ack packet_sent;

    struct packet window[WINDOW_SIZE][BUF_SIZE];
    // which sequence the first cell of window corresponds to
    unsigned int start_sequence = 0;
    // start index of the array
    unsigned int start_index = 0;
    // bool array to indicate if cell is filled
    bool occupied[WINDOW_SIZE];
    FILE *fw;

    for (;;) {
        read_mask = mask;
        idle_interval.tv_sec = 10;
        idle_interval.tv_usec = 0;
        int num = select(FD_SETSIZE, &read_mask, NULL, NULL, &idle_interval);
        if (num > 0) {
            if (FD_ISSET(socket_rcv, &read_mask)) {
                sockaddr_ncp_len = sizeof(sockaddr_ncp);
                // receive packet form ncp and store its address
                int bytes = recvfrom(socket_rcv, &packet_received, sizeof(struct packet), 0,
                        (struct sockaddr*) &sockaddr_ncp, &sockaddr_ncp_len);
                int ncp_ip = sockaddr_ncp.sin_addr.s_addr;
                switch (packet_received.tag) {
                    // if sender wants to start transferring
                    case 0:
                        if (busy) {
                            // send packet to notify busy
                            packet_sent.tag = 2;
                            printf("Sender (%d.%d.%d.%d) wants to connect, but busy\n",
                                    (htonl(ncp_ip) & 0xff000000) >> 24,
                                    (htonl(ncp_ip) & 0x00ff0000) >> 16,
                                    (htonl(ncp_ip) & 0x0000ff00) >> 8,
                                    (htonl(ncp_ip) & 0x000000ff));
                        } else {
                            // open or create file for writing
                            char filename[BUF_SIZE];
                            memcpy(filename, packet_received.file, packet_received.sequence);
                            fw = fopen(filename, "w");
                            if (fw == NULL) {
                                perror("Rcv: cannot open or create file for writing");
                                exit(1);
                            }
                            // send packet to start transfer
                            packet_sent.tag = 0;
                            printf("Establish connection with sender (%d.%d.%d.%d)\n",
                                    (htonl(ncp_ip) & 0xff000000) >> 24,
                                    (htonl(ncp_ip) & 0x00ff0000) >> 16,
                                    (htonl(ncp_ip) & 0x0000ff00) >> 8,
                                    (htonl(ncp_ip) & 0x000000ff));
                        }
                        sendto_dbg(socket_sent, &packet_sent, sizeof(struct packet), 0, 
                                (struct sockaddr*) &sockaddr_ncp, sizeof(sockaddr_ncp));
                        break;

                    // if sender is trasferring
                    case 1:
                    {
                        unsigned int index = convert(packet_received.sequence, start_sequence, start_index);
                        // put buf in the corresponding spot in window
                        memcpy(window[index], packet_received.file, BUF_SIZE);
                        // mark cell as occupied
                        occupied[index] = true;
                        unsigned int new_start_index = start_index;

                        // find the first gap in the window                        
                        unsigned int cur;
                        for (cur = start_sequence; cur < start_sequence + WINDOW_SIZE; cur++) {
                            unsigned int actual_index = convert(cur, start_sequence, start_index);
                            if (!occupied[actual_index]) {
                                break;
                            }
                        }

                        unsigned int gap = cur;
                        // write to file
                        for (cur = start_sequence; cur < gap; cur++) {
                            int bytes_written = fwrite(window[convert(cur, start_sequence, start_index)],
                                    1, BUF_SIZE, fw);
                        }
                        
                        // put ACK in the return packet
                        packet_sent.tag = 1;
                        packet_sent.ack = gap - 1;

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

                            // if no received packet after gap
                            unsigned int last_received = cur;
                            if (last_received == gap - 1) {
                                packet_sent.nums_nack = 0;
                            } else {
                                int counter = 0;
                                // find gaps in between
                                for (cur = gap; cur < last_received; cur++) {
                                    if (!occupied[convert(cur, start_sequence, start_index)]) {
                                        packet_sent.nack[counter] = cur;
                                        counter++;
                                    }
                                }
                                packet_sent.nums_nack = counter;
                            }

                        }
                        }



                        // slide window
                        start_sequence = gap;
                        start_index = (start_index + gap - start_sequence) % WINDOW_SIZE;


                        break;
                    }
                    // if sender sends the last packet
                    case 2:
                        break;
                }


            }
        } else {
            printf(".");
            fflush(0);
        }
    
    }


}


unsigned int convert(unsigned int sequence, unsigned int start_sequence, unsigned int start_index) {
    return (sequence - start_sequence + start_index) % WINDOW_SIZE;
} 
