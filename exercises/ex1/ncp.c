#include "net_include.h"
#include "packet.h"
#include "sendto_dbg.h"
#include "tag.h"

unsigned int convert(unsigned int sequence, unsigned int start_sequence, unsigned int start_index);

// TODO: error checking each ugrad?
// TODO: (low priority) when total_sent_bytes reaches 3 gigabytes, we change unit
int main(int argc, char* argv[]) {
    // args error checking
    if (argc != 4) {
        printf("ncp usage: ncp <loss_rate_percent> <source_file_name> "
                "<dest_file_name>@<comp_name>");
        exit(0);
    }

    int loss_rate_percent = atoi(argv[1]);
    // loss rate error checking
    if (loss_rate_percent == 0 && strcmp(argv[1], "0") != 0) {
        perror("ncp invalid command: loss_rate_percent should be within range [0, "
                "100]\n");
        exit(1);
    }

    sendto_dbg_init(loss_rate_percent);

    // extract computer name and destination file name from args
    char comp_name[8] = {'\0'};
    char* temp = argv[3];
    int comp_char_index = 0;
    bool has_at = false;

    // initialize packet
    struct packet start_packet;
    start_packet.tag = NCP_FILENAME;
    start_packet.sequence = 0;
    start_packet.bytes = 0;

    // parse filename and rcv name from command line
    for (int i = 0; i < strlen(temp); i++) {
        if ((!has_at) && temp[i] != '@') {
            // put filename to packet
            start_packet.file[i] = temp[i];
            // record size of filename in packet
            start_packet.bytes += sizeof(char);
        } else if ((!has_at) && temp[i] == '@') {
            has_at = true;
        } else if (has_at) {
            if (comp_char_index == 7) {
                perror("ncp invalid command: <comp_name> is too long\n");
                exit(1);
            }
            comp_name[comp_char_index++] = temp[i];
        }
    }
    if (!has_at) {  // prompt error when there's no '@' in the argument
        perror("ncp invalid command: incorrect format in "
                "<dest_file_name>@<comp_name>\n");
        exit(1);
    }

    // address of ncp
    struct sockaddr_in name;
    // address of incoming packets
    struct sockaddr_in send_addr;
    // address of rcv
    struct sockaddr_in from_addr;
    socklen_t from_len;

    // socket both for sending and receiving
    int sk = socket(AF_INET, SOCK_DGRAM, 0);
    if (sk < 0) {
        perror("ncp: socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    // bind socket with port
    if (bind(sk, (struct sockaddr*)&name, sizeof(name)) < 0) {
        perror("ncp: bind");
        exit(1);
    }

    // get rcv address
    struct hostent* rcv_name;
    struct hostent rcv_name_copy;
    int rcv_fd;
    rcv_name = gethostbyname(comp_name);
    if (rcv_name == NULL) {
        perror("ncp: invalid receiver name\n");
        exit(1);
    }
    memcpy(&rcv_name_copy, rcv_name, sizeof(rcv_name_copy));
    memcpy(&rcv_fd, rcv_name_copy.h_addr_list[0], sizeof(rcv_fd));

    // send send_addr to be rcv address
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = rcv_fd;
    send_addr.sin_port = htons(PORT);

    fd_set mask;
    fd_set read_mask;
    int num;

    FD_ZERO(&mask);
    FD_SET(sk, &mask);

    FILE* source_file;  // pointer to source file
    if ((source_file = fopen(argv[2], "r")) == NULL) {
        perror("ncp: fopen");
        exit(1);
    }

    printf("Open file %s for transferring\n", argv[2]);

    // timer for retransmission
    struct timeval timeout;

    // packet to send to rcv
    struct packet_mess mess_pac;
    // window to store packets to send
    struct packet win[WINDOW_SIZE];

    int curr_ind_zero = 0;
    int curr_ind = 0;  // the index which is the index the sender sended up to
    
    int start_sequence = 0;
    int start_index = 0;

    // if have established connection with rcv
    bool begin = false;
    // indicating if the file chunk read is the last packet
    bool last_packet = false;
    int last_ind = 0; 
    int last_sequence = 0;
    
    struct packet temp_pac;

    // send filename to rcv
    sendto_dbg(sk, (char*)&start_packet, sizeof(struct packet), 0,
                         (struct sockaddr*)&send_addr, sizeof(send_addr));

    printf("Try to establish connection with (%d.%d.%d.%d)\n",
                 (htonl(send_addr.sin_addr.s_addr) & 0xff000000) >> 24,
                 (htonl(send_addr.sin_addr.s_addr) & 0x00ff0000) >> 16,
                 (htonl(send_addr.sin_addr.s_addr) & 0x0000ff00) >> 8,
                 (htonl(send_addr.sin_addr.s_addr) & 0x000000ff));

    clock_t begin_total_t, sent_start_t, sent_end_t, finish_total_t;

    unsigned int sent_bytes = 0;  // sent bytes
    unsigned int total_sent_bytes = 0;

    for (;;) {
        read_mask = mask;
        timeout.tv_sec = NACK_INTERVAL;
        timeout.tv_usec = 0;
        num = select(FD_SETSIZE, &read_mask, NULL, NULL, &timeout);
        if (num > 0) {
            if (FD_ISSET(sk, &read_mask)) {
                from_len = sizeof(from_addr);
                // TODO: What if receive an ack from NOT current rcv?
                recvfrom(sk, &mess_pac, sizeof(struct packet_mess), 0,
                                 (struct sockaddr*)&from_addr, &from_len);
                int rcv_ip = from_addr.sin_addr.s_addr;

                switch (mess_pac.tag) {
                    // receiver is not busy, we can get started
                    case RCV_START:
                    {
                        begin = true;
                        begin_total_t = clock();
                        sent_start_t = clock();
                        
                        // initialize window and send each packet after each read
                        for (int i = 0; i < WINDOW_SIZE; i++) {
                            if (last_packet) {  // break and stop reading if last packet was
                                                                    // read
                                break;
                            }
                            temp_pac.tag = NCP_FILE;
                            temp_pac.bytes = fread(temp_pac.file, 1, BUF_SIZE, source_file);
                            temp_pac.sequence = i;
                            if (temp_pac.bytes < BUF_SIZE) {
                                if (feof(source_file)) {  // when we reach the EOF
                                    printf("Finished reading.\n");
                                    temp_pac.tag = NCP_LAST;
                                    last_packet = true;
                                    last_ind = curr_ind; 
                                    last_sequence = i;
                                } else {
                                    printf("An error occurred when finished reading...\n");
                                    exit(0);
                                }
                            }
                            printf("read file with sequence %d bytes %d to window index %d\n", temp_pac.sequence, temp_pac.bytes, i);
                            // TODO: change to malloc to avoid copying
                            win[i] = temp_pac;
                            printf("send packet of bytes %d sequence %d\n", win[i].bytes, win[i].sequence);
                            sendto_dbg(sk, (char*)&temp_pac, sizeof(struct packet), 0,
                                                 (struct sockaddr*)&send_addr, sizeof(send_addr));
                            curr_ind = i;
                        }
                        break;
                    
                    }

                    // in the process of transferring packets.
                    // receives ack and nack
                    case RCV_ACK:
                    {
                        printf("----------------START-----------------\n");
                        // if ack changes, slide window and send new packets
                        printf("got ack %d, nums_nack %d\n", mess_pac.ack, mess_pac.nums_nack);
                        if ((mess_pac.ack != UINT_MAX && mess_pac.ack >= start_sequence)) {
                            unsigned int cur;
                            for (cur = start_sequence + WINDOW_SIZE;
                                cur <= mess_pac.ack + WINDOW_SIZE; cur++) {
                                
                                // if already read the last packet, no need to slide the window
                                if (last_packet) {
                                    break;
                                }
                                temp_pac.tag = NCP_FILE;
                                temp_pac.bytes = fread(temp_pac.file, 1, BUF_SIZE, source_file);

                                temp_pac.sequence = cur;
                                if (temp_pac.bytes < BUF_SIZE) {
                                    if (feof(source_file)) {
                                        printf("Read last pakcet of size %d sequence %d.\n", temp_pac.bytes, temp_pac.sequence);
                                        last_packet = true;
                                        temp_pac.tag = NCP_LAST;
                                        last_ind = curr_ind;
                                        last_sequence = cur;
                                    } else {
                                        printf("An error occurred when finishing reading\n");
                                        exit(0);
                                    }
                                }

                                printf("read file withsequence %d bytes %d to window index %d", temp_pac.sequence, temp_pac.bytes, convert(cur, start_sequence, start_index));

                                win[convert(cur, start_sequence, start_index)] = temp_pac;
                                printf("send packet of bytes %d sequence %d\n", win[convert(cur, start_sequence, start_index)].bytes, win[convert(cur, start_sequence, start_index)].sequence);
                                sendto_dbg(sk, (char*)&win[convert(cur, start_sequence, start_index)],
                                    sizeof(struct packet), 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                            

                            }
                        }

                            // if NACK exists
                        if (mess_pac.nums_nack > 0) {
                            unsigned int nack_sequence;
                            for (int i = 0; i < mess_pac.nums_nack; i++) {
                                memcpy(&nack_sequence, &(mess_pac.nack[i]), sizeof(unsigned int));
                                printf("nack is %d\n", nack_sequence);
                                printf("Retransmit packet bytes %d sequence %d\n", win[convert(nack_sequence, start_sequence, start_index)].bytes, win[convert(nack_sequence, start_sequence, start_index)].sequence);
                                if (nack_sequence != win[convert(nack_sequence, start_sequence, start_index)].sequence) {
                                    printf("Warning: NOT retransmit correct nack %d, but sent %d\n", nack_sequence, win[convert(nack_sequence, start_sequence, start_index)].sequence);
                                }
                                sendto_dbg(sk, (char*)&win[convert(nack_sequence, start_sequence, start_index)],
                                    sizeof(struct packet), 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                            }
                        }

                        
                        if (mess_pac.ack != UINT_MAX) {
                            start_index = (start_index + mess_pac.ack + 1 - start_sequence) % WINDOW_SIZE;
                            start_sequence = mess_pac.ack + 1;
                            printf("start_sequence is now %d, start_index %d\n", start_sequence, start_index);
                        }

                        printf("-----------------END------------------\n\n");
                        break;

                         /* 
                        // rcv didn't get the very first packet
                        // TODO: can also send NACK in this case
                        if (mess_pac.ack == UINT_MAX) {
                            sendto_dbg(sk, (char*)&win[curr_ind_zero], sizeof(struct packet),
                                                 0, (struct sockaddr*)&send_addr, sizeof(send_addr));

                            break;
                        }

                        unsigned int nums_nack = mess_pac.nums_nack;
                        int first_sequence = win[curr_ind_zero].sequence;

                        // difference between this ack and last ack
                        int offset = mess_pac.ack - first_sequence + 1;
                        // enters the loop if offset is positive
                        for (int i = 0; i < offset; i++) {
                            if (last_packet) {  // break and stop reading if lask packet was
                                                                    // read
                                break;
                            }
                            temp_pac.tag = NCP_FILE;
                            temp_pac.bytes = fread(temp_pac.file, 1, BUF_SIZE, source_file);

                            temp_pac.sequence = win[curr_ind].sequence + 1;
                            if (temp_pac.bytes < BUF_SIZE) {
                                if (feof(source_file)) {
                                    printf("Finished reading.\n");
                                    last_packet = true;
                                    temp_pac.tag = NCP_LAST;
                                    last_ind = curr_ind; 
                                } else {
                                    printf("An error occurred when finishing reading\n");
                                    exit(0);
                                }
                            }
                            sent_bytes += win[curr_ind].bytes;
                            win[curr_ind_zero] = temp_pac;
                            curr_ind_zero = (curr_ind_zero + 1) % WINDOW_SIZE;
                            
                            //printf("send packet bytes %d with sequence %d\n", temp_pac.bytes, temp_pac.sequence);
                            sendto_dbg(sk, (char*)&temp_pac, sizeof(struct packet), 0,
                                                 (struct sockaddr*)&send_addr, sizeof(send_addr));
                            curr_ind = (curr_ind + 1) % WINDOW_SIZE;
                            // report sent bytes
                            if (sent_bytes >= 100000000) {
                                    total_sent_bytes += sent_bytes;
                                    sent_end_t = clock();
                                     printf("%.2fMbytes sent with an average transfer rate %.2fMbits/sec!\n",
                                    (double)sent_bytes / 1000000,
                                 (double)(sent_bytes / 125000) /
                                 ((double)(sent_end_t - sent_start_t) / CLOCKS_PER_SEC));
            sent_bytes = 0;
            sent_start_t = clock();
        }
                        }
                        if (nums_nack > 0) {
                            for (int i = 0; i < nums_nack; i++) {
                                int nack_ind = (mess_pac.nack[i] - win[curr_ind_zero].sequence +
                                                                curr_ind_zero) %
                                                             WINDOW_SIZE;
                                sendto_dbg(sk, (char*)&win[nack_ind], sizeof(struct packet), 0,
                                                     (struct sockaddr*)&send_addr, sizeof(send_addr));
                            }
                        }
                        break;
    */        
                    }

                    // receiver is busy
                    case RCV_BUSY:
                    {
                        printf("Rcv (%d.%d.%d.%d) is busy right now, will retry to connect in 10 seconds\n",
                            (htonl(rcv_ip) & 0xff000000) >> 24,
                            (htonl(rcv_ip) & 0x00ff0000) >> 16,
                            (htonl(rcv_ip) & 0x0000ff00) >> 8,
                            (htonl(rcv_ip) & 0x000000ff));
                        sleep(8);  // sleep for 8 seconds
                        break;
                    }

                    case RCV_END:  // receiver respond finished
/*            finish_total_t = clock();
                        // TODO: report size of file, average rate at which communication
                        // occurred.
                        total_sent_bytes += win[last_ind].bytes+sent_bytes;
                        printf("Total file size is %d bytes\n", total_sent_bytes);
                        printf("Total file size is %.2f Mbytes\n",
                                     (double)total_sent_bytes / 1000000);
                        double time =
                                (double)(finish_total_t - begin_total_t) / CLOCKS_PER_SEC;
                        printf("Total time used %f sec\n", time);
                        printf("Average file transfer rate is %.2fMbits/sec\n",
                                     (total_sent_bytes / 125000) / time);

             */
                        printf("File Transfer Completed!\n");
                        fclose(source_file);
                        exit(0);
                        break;
                }

                if (!begin) {
                    sendto_dbg(sk, (char*)&start_packet, sizeof(struct packet), 0,
                                         (struct sockaddr*)&send_addr, sizeof(send_addr));
                }
            }
        } else {  // when timeout
            printf("Haven't heard response for over %d seconds, timeout\n", NACK_INTERVAL);
            if (!begin) {
                sendto_dbg(sk, (char*)&start_packet, sizeof(struct packet), 0,
                                     (struct sockaddr*)&send_addr, sizeof(send_addr));

            } else {
                unsigned int last;
                if (last_packet) {
                    last = last_sequence;
                } else {
                    last = start_sequence + WINDOW_SIZE - 1;
                }
                printf("Send LAST packet in window bytes %d sequence %d\n", win[convert(last, start_sequence, start_index)].bytes, win[convert(last, start_sequence, start_index)].sequence);
                sendto_dbg(sk, (char*)&win[convert(last, start_sequence, start_index)], sizeof(struct packet), 0,
                                     (struct sockaddr*)&send_addr, sizeof(send_addr));
            }
            //   fflush(0);
        }

    }  // ending for loop
    return 0;
}

unsigned int convert(unsigned int sequence, unsigned int start_sequence,
                                         unsigned int start_index) {
    return (sequence - start_sequence + start_index) % WINDOW_SIZE;
}
