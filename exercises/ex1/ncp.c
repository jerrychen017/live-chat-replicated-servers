#include "net_include.h"
#include "packet.h"
#include "sendto_dbg.h"
#include "tag.h"
#include "helper.h"

// TODO: error checking each ugrad?
// TODO: (low priority) when total_sent_bytes reaches 3 gigabytes, we change unit
int main(int argc, char* argv[]) {
    // args error checking
    if (argc != 4) {
        printf("ncp usage: ncp <loss_rate_percent> <source_file_name> "
                "<dest_file_name>@<comp_name>\n");
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

    // sequence which the first cell of window corresponds to
    int start_sequence = 0;
    // start index of the array
    int start_index = 0;

    // if have established connection with rcv
    bool begin = false;
    // indicating if the file chunk read is the last packet
    bool last_packet = false;
    // sequence of the last packet
    int last_sequence = 0;
    
    // send filename to rcv
    sendto_dbg(sk, (char*)&start_packet, sizeof(struct packet), 0,
                         (struct sockaddr*)&send_addr, sizeof(send_addr));

    printf("Try to establish connection with (%d.%d.%d.%d)\n",
                 (htonl(send_addr.sin_addr.s_addr) & 0xff000000) >> 24,
                 (htonl(send_addr.sin_addr.s_addr) & 0x00ff0000) >> 16,
                 (htonl(send_addr.sin_addr.s_addr) & 0x0000ff00) >> 8,
                 (htonl(send_addr.sin_addr.s_addr) & 0x000000ff));

    unsigned int bytes = 0;
    unsigned int buf_size = 0;
    unsigned int last_packet_size = 0;

    // record starting time of transfer
    struct timeval start_time;
    struct timeval last_time;
    unsigned int sent_num = 0;
    unsigned int received_num = 0;
    double loss_rate = 0;
    __suseconds_t timeout_usec = NACK_INTERVAL_USEC;

    for (;;) {
        // detects loss rate based on number of acks received by rcv
        if (begin && sent_num != 0 && received_num != 0) {
            loss_rate = (double)(sent_num - received_num) / sent_num;
        if (loss_rate >= 0 && loss_rate <= 0.05) {
            timeout_usec = 6000;
        } else if (loss_rate > 0.05 && loss_rate <= 0.1) {
          timeout_usec = 3000;
        } else if (loss_rate > 0.1 && loss_rate <= 0.2) {
          timeout_usec = 2000;
        } else if (loss_rate > 0.2 && loss_rate <= 0.3) {
          timeout_usec = 1000;
        } else {
          timeout_usec = 1000;
        }
      }
        read_mask = mask;
        timeout.tv_sec = NACK_INTERVAL_SEC;
        timeout.tv_usec = timeout_usec;
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
                        if (begin) {
                            break;
                        }
                        printf("-----------------START------------------\n");
                        begin = true;
                        // record starting time
                        gettimeofday(&start_time, NULL);                        
                        gettimeofday(&last_time, NULL);
                        // initialize window and send each packet after each read
                        for (int i = 0; i < WINDOW_SIZE; i++) {
                            // break and stop reading if last packet was read
                            if (last_packet) {
                                break;
                            }
                            win[i].tag = NCP_FILE;
                            win[i].bytes = fread(win[i].file, 1, BUF_SIZE, source_file);
                            win[i].sequence = i;
                            received_num++;
                            if (win[i].bytes < BUF_SIZE) {
                                if (feof(source_file)) {  // when we reach the EOF
                                    win[i].tag = NCP_LAST;
                                    last_packet = true;
                                    last_sequence = i;
                                    last_packet_size = win[i].bytes;
                                } else {
                                    printf("An error occurred when finished reading...\n");
                                    exit(0);
                                }
                            }

                            // save buffer size
                            if (i == 0 && !last_packet) {
                                buf_size = win[i].bytes;
                            }

                            sendto_dbg(sk, (char*)&win[i], sizeof(struct packet), 0,
                                                 (struct sockaddr*)&send_addr, sizeof(send_addr));
                            sent_num++;
                        }

                        break;
                    
                    }

                    // in the process of transferring packets.
                    // receives ack and nack
                    case RCV_ACK:
                    {
                        //print_packet_mess(&mess_pac);
                        // if ack changes, slide window and send new packets
                        if ((mess_pac.ack != UINT_MAX && mess_pac.ack >= start_sequence)) {
                            
                            unsigned int old_bytes = bytes;
                            bytes += (mess_pac.ack - start_sequence + 1) * buf_size;

                            if (old_bytes / 100000000 != bytes / 100000000) {
                                struct timeval current_time;
                                gettimeofday(&current_time, NULL);
                                struct timeval diff_time = diffTime(current_time, last_time);
                                double seconds = diff_time.tv_sec + ((double) diff_time.tv_usec) / 1000000;
                                printf("Report: total amount of data transferred is %u Mbytes\n", bytes / (1024 * 1024));
                                printf("        average transfer rate of the last 100 Mbytes received is %.2f Mbits/sec\n", (double) (100) * 8 / seconds);
                                last_time = current_time;
                            }
                            
                            unsigned int cur;
                            unsigned int index;
                            for (cur = start_sequence + WINDOW_SIZE;
                                cur <= mess_pac.ack + WINDOW_SIZE; cur++) {
                                
                                // if already read the last packet, no need to slide the window
                                if (last_packet) {
                                    break;
                                }
                                index = convert(cur, start_sequence, start_index);
                                win[index].tag = NCP_FILE;
                                win[index].bytes = fread(win[index].file, 1, BUF_SIZE, source_file);
                                received_num++;
                                win[index].sequence = cur;
                                if (win[index].bytes < BUF_SIZE) {
                                    if (feof(source_file)) {
                                        last_packet = true;
                                        win[index].tag = NCP_LAST;
                                        last_sequence = cur;
                                        last_packet_size = win[index].bytes;
                                    } else {
                                        printf("An error occurred when finishing reading\n");
                                        exit(0);
                                    }
                                }

                                sendto_dbg(sk, (char*)&win[index], sizeof(struct packet), 0,
                                        (struct sockaddr*)&send_addr, sizeof(send_addr));
                            
                                sent_num++;
                            }
                        }

                            // if NACK exists
                        if (mess_pac.nums_nack > 0) {
                            unsigned int nack_sequence;
                            for (int i = 0; i < mess_pac.nums_nack; i++) {
                                memcpy(&nack_sequence, &(mess_pac.nack[i]), sizeof(unsigned int));
                                if (nack_sequence != win[convert(nack_sequence, start_sequence, start_index)].sequence) {
                                    printf("Warning: NOT retransmit correct nack %d, but sent %d\n", nack_sequence, win[convert(nack_sequence, start_sequence, start_index)].sequence);
                                }
                                sendto_dbg(sk, (char*)&win[convert(nack_sequence, start_sequence, start_index)],
                                    sizeof(struct packet), 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                                sent_num++;
                            }
                        }

                        
                        if (mess_pac.ack != UINT_MAX) {
                            start_index = (start_index + mess_pac.ack + 1 - start_sequence) % WINDOW_SIZE;
                            start_sequence = mess_pac.ack + 1;
                        }

                        break;

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

                    // receiver has finished transfer
                    case RCV_END:
                    {
                        bytes += (last_sequence - start_sequence) * buf_size + last_packet_size;

                        printf("Finish trasnferring file with receiver (%d.%d.%d.%d)\n",
                                    (htonl(rcv_ip) & 0xff000000) >> 24,
                                    (htonl(rcv_ip) & 0x00ff0000) >> 16,
                                    (htonl(rcv_ip) & 0x0000ff00) >> 8,
                                    (htonl(rcv_ip) & 0x000000ff));
                        
                        struct timeval end_time;
                        gettimeofday(&end_time, NULL);
                        struct timeval diff_time = diffTime(end_time, start_time);
                        double seconds = diff_time.tv_sec + ((double) diff_time.tv_usec) / 1000000;
                        printf("Report: Size of file transferred is %.2f Mbytes\n", (double) bytes / (1024 * 1024));
                        printf("        Amount of time spent is %.2f seconds\n", seconds);
                        printf("        Average rate is %.2f Mbits/sec\n",
                                    (double) bytes / (1024 * 1024) * 8 / seconds);
                        printf("------------------END-------------------\n");
                        
                        fclose(source_file);
                        exit(0);
                        break;
                    }
                }

                if (!begin) {
                    sendto_dbg(sk, (char*)&start_packet, sizeof(struct packet), 0,
                                         (struct sockaddr*)&send_addr, sizeof(send_addr));
                    sent_num++; 
                }
            }
        } else {
            //printf("Haven't heard response for over %d seconds, timeout!\n", NACK_INTERVAL);
            if (!begin) {
                printf("Resend connect request to (%d.%d.%d.%d)\n",
                        (htonl(send_addr.sin_addr.s_addr) & 0xff000000) >> 24,
                        (htonl(send_addr.sin_addr.s_addr) & 0x00ff0000) >> 16,
                        (htonl(send_addr.sin_addr.s_addr) & 0x0000ff00) >> 8,
                        (htonl(send_addr.sin_addr.s_addr) & 0x000000ff));
                sendto_dbg(sk, (char*)&start_packet, sizeof(struct packet), 0,
                                     (struct sockaddr*)&send_addr, sizeof(send_addr));
                sent_num++; 

            } else {
                unsigned int last;
                if (last_packet) {
                    last = last_sequence;
                } else {
                    last = start_sequence + WINDOW_SIZE - 1;
                }
                //printf("retransmit\n");
                // retransmit LAST packet in the window
                sendto_dbg(sk, (char*)&win[convert(last, start_sequence, start_index)], sizeof(struct packet), 0,
                                     (struct sockaddr*)&send_addr, sizeof(send_addr));
                sent_num++;
            }
            fflush(0);
        }

    } // ending for loop
    return 0;
}
