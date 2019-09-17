#include "net_include.h"
#include "sendto_dbg.h"
#include "packet.h"

int convert(int index, int start_index);

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
        eixt(1);
    }

    struct sockaddr_in sockaddr_rcv;
    sockaddr_rcv.sin_family = AF_INET;
    sockaddr_rcv.sin_addr.s_addr = INADDR_ANY;
    sockaddr_rcv.sin_port = htons(PORT);

    // bind the socket with rcv address
    if (bind(socket_rcv, (struct aockaddr*) &sockaddr_rcv, sizeof(sockaddr_rcv)) < 0) {
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
    struct packet packet_sent;

    struct packet window[WINDOW_SIZE][BUF_SIZE];
    unsigned int start_sequence = 0;
    bool occupied[WINDOW_SIZE];

    for (;;) {
        read_mask = mask;
        idle_interval.tv_sec = 10;
        idle_interval.tv_usec = 0;
        int num = select(FD_SETSIZE, &read_mask, NULL, NULL, &timeout);
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
                        // put buf in the corresponding spot in window
                        memcpy(window[packet_received.sequence - start_sequence], packet_received.file, BUF_SIZE);
                        
                        break;

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


int convert(int index, int start_index) {
    return (start_index + index) % WINDOW_SIZE;
} 
