#include "net_include.h"
#include "packet.h"
#include "sendto_dbg.h"
#include "tag.h"

#define NAME_LENGTH 80

// DISCUSS: error checking each ugrad?

int main(int argc, char* argv[]) {
  // args error checking
  if (argc != 4) {
    printf(
        "ncp usage: ncp <loss_rate_percent> <source_file_name> "
        "<dest_file_name>@<comp_name>");
    exit(0);
  }

  int loss_rate_percent = atoi(argv[1]);
  // loss rate error checking
  if (loss_rate_percent == 0 && strcmp(argv[1], "0") != 0) {
    perror(
        "ncp invalid command: loss_rate_percent should be within range [0, "
        "100]\n");
    exit(1);
  }
  sendto_dbg_init(loss_rate_percent);

  // extract computer name and destination file name from args
  char comp_name[8] = {'\0'};  // ugrad machines name

  struct packet start_packet;
  start_packet.tag = NCP_FILENAME;
  start_packet.sequence = 0;
  start_packet.bytes = 0;
  char* temp = argv[3];
  int comp_char_index = 0;
  bool has_at = false;

  // parse rcv name from command line
  for (int i = 0; i < strlen(temp); i++) {
    if ((!has_at) && temp[i] != '@') {
      start_packet.file[i] = temp[i];
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
    perror(
        "ncp invalid command: incorrect format in "
        "<dest_file_name>@<comp_name>\n");
    exit(1);
  }

  // address of ncp
  struct sockaddr_in name;
  // address of incoming requests
  struct sockaddr_in send_addr;
  // address of rcv
  struct sockaddr_in from_addr;
  socklen_t from_len;

  // int from_ip;
  // socket both for sending and receiving
  int sk;
  fd_set mask;
  fd_set read_mask;
  int num;
  struct timeval timeout;

  // socket for receiving messages
  sk = socket(AF_INET, SOCK_DGRAM, 0);
  if (sk < 0) {
    perror("ncp: socket");
    exit(1);
  }

  name.sin_family = AF_INET;
  name.sin_addr.s_addr = INADDR_ANY;
  name.sin_port = htons(PORT);

  if (bind(sk, (struct sockaddr*)&name, sizeof(name)) < 0) {
    perror("ncp: bind");
    exit(1);
  }

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

  send_addr.sin_family = AF_INET;
  send_addr.sin_addr.s_addr = rcv_fd;
  send_addr.sin_port = htons(PORT);

  FD_ZERO(&mask);
  FD_SET(sk, &mask);

  FILE* source_file;  // pointer to source file
  if ((source_file = fopen(argv[2], "r")) == NULL) {
    perror("fopen");
    exit(0);
  }

  printf("Opened %s for reading...\n", argv[2]);

  struct packet_mess mess_pac;
  struct packet win[WINDOW_SIZE];
  int curr_ind_zero = 0;
  int curr_ind = 0;  // the index which is the index the sender sended up to
  bool begin = false;
  // indicating if the file chunk read is the last packet
  bool last_packet = false;
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
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    num = select(FD_SETSIZE, &read_mask, NULL, NULL, &timeout);
    if (num > 0) {
      // receiving message
      if (FD_ISSET(sk, &read_mask)) {
        from_len = sizeof(from_addr);
        recvfrom(sk, &mess_pac, sizeof(struct packet_mess), 0,
                 (struct sockaddr*)&from_addr, &from_len);
        // from_ip = from_addr.sin_addr.s_addr;
        // printf("Received Ack and Nack message from (%d.%d.%d.%d): ack is
        // %d\n",
        //        (htonl(from_ip) & 0xff000000) >> 24,
        //        (htonl(from_ip) & 0x00ff0000) >> 16,
        //        (htonl(from_ip) & 0x0000ff00) >> 8,
        //        (htonl(from_ip) & 0x000000ff), mess_pac.ack);

        switch (mess_pac.tag) {
          // receiver is not busy, we can get started
          case RCV_START:
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

                } else {
                  printf("An error occurred when finished reading...\n");
                  exit(0);
                }
              }
              win[i] = temp_pac;
              sendto_dbg(sk, (char*)&temp_pac, sizeof(struct packet), 0,
                         (struct sockaddr*)&send_addr, sizeof(send_addr));
              curr_ind = i;
            }
            break;
          // in the process of transferring packets.
          // receives ack and nack
          case RCV_ACK:
            // rcv didn't get the very first packet
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
                } else {
                  printf("An error occurred when finishing reading\n");
                  exit(0);
                }
              }
              sent_bytes += win[curr_ind].bytes;
              win[curr_ind_zero] = temp_pac;
              curr_ind_zero = (curr_ind_zero + 1) % WINDOW_SIZE;
              sendto_dbg(sk, (char*)&temp_pac, sizeof(struct packet), 0,
                         (struct sockaddr*)&send_addr, sizeof(send_addr));
              curr_ind = (curr_ind + 1) % WINDOW_SIZE;
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
          // receiver is busy
          case RCV_BUSY:
            printf("RCV is busy!\n");
            sleep(10);  // sleep for 10 seconds and then come back
            break;
          case RCV_END:  // receiver respond finished
            finish_total_t = clock();
            // TODO: report size of file, average rate at which communication
            // occurred.
            total_sent_bytes += win[curr_ind].bytes;
            printf("Total file size is %.2f Mbytes\n",
                   (double)total_sent_bytes / 1000000);
            double time =
                (double)(finish_total_t - begin_total_t) / CLOCKS_PER_SEC;
            printf("Total time used %f sec\n", time);
            printf("Average file transfer rate is %.2fMbits/sec\n",
                   (total_sent_bytes / 125000) / time);
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
      printf("timeout! \n");
      if (!begin) {
        sendto_dbg(sk, (char*)&start_packet, sizeof(struct packet), 0,
                   (struct sockaddr*)&send_addr, sizeof(send_addr));

      } else {
        sendto_dbg(sk, (char*)&win[curr_ind], sizeof(struct packet), 0,
                   (struct sockaddr*)&send_addr, sizeof(send_addr));
      }
      //   fflush(0);
    }

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

  }  // ending for loop
  return 0;
}
