#include "net_include.h"
#include "packet.h"
#include "sendto_dbg.h"

#define NAME_LENGTH 80

int main(int argc, char** argv) {
  // args error checking
  if (argc != 4) {
    printf(
        "Usage: ncp <loss_rate_percent> <source_file_name> "
        "<dest_file_name>@<comp_name>");
    exit(0);
  }

  char comp_name[NAME_LENGTH] = {'\0'};
  struct packet start_packet;
  start_packet.tag = 0;
  start_packet.sequence = 0;
  start_packet.bytes = 0;
  //   char dest_file_name[NAME_LENGTH] = {'\0'};

  int loss_rate_percent = atoi(argv[1]);
  if ((loss_rate_percent <= 0 && strcmp(argv[1], "0") != 0) ||
      loss_rate_percent > 100) {
    perror("ncp: loss_rate_percent should be within range [0, 100]");
    exit(1);
  }

  sendto_dbg_init(loss_rate_percent);

  // extract computer name and destination file name from args
  char* temp = argv[3];
  int comp_char_index = 0;
  bool has_at = false;
  for (int i = 0; i < strlen(temp); i++) {
    if ((!has_at) && temp[i] != '@') {
      start_packet.file[i] = temp[i];
      start_packet.bytes += sizeof(char);
    } else if ((!has_at) && temp[i] == '@') {
      has_at = true;
    } else if (has_at) {
      comp_name[comp_char_index++] = temp[i];
    }
  }

  if (!has_at) {  // prompt error
    perror("invalid: <dest_file_name>@<comp_name>");
    exit(1);
  }

  struct sockaddr_in name;
  struct sockaddr_in send_addr;
  struct sockaddr_in from_addr;
  socklen_t from_len;
  struct hostent h_ent;
  struct hostent* p_h_ent;
  int host_num;
  int from_ip;
  int ss, sr;
  fd_set mask;
  fd_set read_mask, write_mask, excep_mask;
  int bytes;
  int num;
  char my_name[NAME_LENGTH] = {'\0'};
  struct timeval timeout;

  // socket for receiving messages
  sr = socket(AF_INET, SOCK_STREAM, 0);
  if (sr < 0) {
    perror("ncp: socket");
    exit(1);
  }

  name.sin_family = AF_INET;
  name.sin_addr.s_addr = INADDR_ANY;
  name.sin_port = htons(PORT);

  if (bind(sr, (struct sockaddr*)&name, sizeof(name)) < 0) {
    perror("ncp: bind");
    exit(1);
  }

  ss = socket(AF_INET, SOCK_DGRAM, 0); /* socket for sending (udp) */
  if (ss < 0) {
    perror("ncp: socket");
    exit(1);
  }

  gethostname(my_name, NAME_LENGTH);  // gets my name
  p_h_ent = gethostbyname(comp_name);
  if (p_h_ent == NULL) {
    printf("ncp: gethostbyname error.\n");
    exit(1);
  }

  memcpy(&h_ent, p_h_ent, sizeof(h_ent));
  memcpy(&host_num, h_ent.h_addr_list[0], sizeof(host_num));

  send_addr.sin_family = AF_INET;
  send_addr.sin_addr.s_addr = host_num;
  send_addr.sin_port = htons(PORT);

  FD_ZERO(&mask);
  FD_ZERO(&write_mask);
  FD_ZERO(&excep_mask);
  FD_SET(sr, &mask);
  FD_SET((long)0, &mask); /* stdin */

  FILE* source_file;  // pointer to source file
  if ((source_file = fopen(argv[2], "r")) == NULL) {
    perror("fopen");
    exit(0);
  }

  printf("Opened %s for reading...\n", argv[2]);

  struct packet_mess mess_pac;
  struct packet win[WINDOW_SIZE];
  int curr_ind_zero = 0;
  int curr_ind;  // the index which is one over the index the sender sended up
                 // to
  char buffer[BUF_SIZE];  // to store a chunk of file
  bool begin = false;
  bool finished = false;
  // indicating if the file chunk read is the last packet
  bool last_packet = false;
  int last_sequence = -1;
  struct packet temp_pac;

  for (;;) {
    read_mask = mask;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    num = select(FD_SETSIZE, &read_mask, &write_mask, &excep_mask, &timeout);
    if (num > 0) {
      // receiving message
      if (FD_ISSET(sr, &read_mask)) {
        from_len = sizeof(from_addr);
        bytes = recvfrom(sr, &mess_pac, sizeof(struct packet_mess), 0,
                         (struct sockaddr*)&from_addr, &from_len);
        // mess_buf[bytes] = 0;

        from_ip = from_addr.sin_addr.s_addr;
        printf("Received Ack and Nack message from (%d.%d.%d.%d): ack is %d\n",
               (htonl(from_ip) & 0xff000000) >> 24,
               (htonl(from_ip) & 0x00ff0000) >> 16,
               (htonl(from_ip) & 0x0000ff00) >> 8,
               (htonl(from_ip) & 0x000000ff), mess_pac.ack);

        switch (mess_pac.tag) {
          // receiver is not busy, we can get started
          case 0:
            begin = true;
            // initialize window
            for (int i = 0; i < WINDOW_SIZE; i++) {
              temp_pac.tag = 1;
              temp_pac.bytes = fread(buffer, 1, BUF_SIZE, source_file);
              temp_pac.sequence = i;
              for (int j = 0; j < BUF_SIZE; j++) {  // copy arr
                temp_pac.file[j] = buffer[j];
              }
              win[i] = temp_pac;
              if (temp_pac.bytes < BUF_SIZE) {
                /* Did we reach the EOF? */
                if (feof(source_file)) {
                  printf("Finished reading.\n");
                  temp_pac.tag = 2;
                  last_packet = true;
                  last_sequence = i;
                  break;
                } else {
                  printf("An error occurred...\n");
                  exit(0);
                }
              }
            }
            sendto_dbg(ss, (char*)&win[0], sizeof(struct packet), 0,
                       (struct sockaddr*)&send_addr, sizeof(send_addr));
            curr_ind = 1;
            break;
          // in the process of transferring packets.
          // receives ack and nack
          case 1:
            // meaning that ack is different than before
            if (mess_pac.ack == last_sequence) {
              finished = true;
              temp_pac.tag = 3;
              sendto_dbg(ss, (char*)&temp_pac, sizeof(struct packet), 0,
                         (struct sockaddr*)&send_addr, sizeof(send_addr));
              break;
            }
            unsigned int nums_nack = mess_pac.nums_nack;
            int first_sequence = win[curr_ind_zero].sequence;
            int offset = mess_pac.ack - first_sequence;

            if (offset >= 0) {
              if (!last_packet) {  // slide window if not last packet
                for (int i = 0; i < offset + 1; i++) {
                  temp_pac.tag = 1;
                  temp_pac.bytes = fread(buffer, 1, BUF_SIZE, source_file);
                  temp_pac.sequence = offset + +1;
                  if (temp_pac.bytes < BUF_SIZE) {
                    if (feof(source_file)) {
                      printf("Finished reading.\n");
                      last_packet = true;
                      last_sequence = temp_pac.sequence;
                      temp_pac.tag = 2;
                      break;
                    } else {
                      printf("An error occurred when finishing reading\n");
                      exit(0);
                    }
                  }
                  if (curr_ind_zero == WINDOW_SIZE - 1) {
                    curr_ind_zero = 0;
                  } else {
                    curr_ind_zero++;
                  }
                }
              }

              if (nums_nack == 0) {  // nack is empty
                if (last_packet) {   // sending last packet
                  int last_ind = last_sequence - win[curr_ind_zero].sequence +
                                 curr_ind_zero;
                  if (last_ind >= WINDOW_SIZE) {
                    last_ind -= WINDOW_SIZE;
                  }
                  sendto_dbg(ss, (char*)&win[last_ind], sizeof(struct packet),
                             0, (struct sockaddr*)&send_addr,
                             sizeof(send_addr));
                } else {
                  sendto_dbg(ss, (char*)&win[curr_ind], sizeof(struct packet),
                             0, (struct sockaddr*)&send_addr,
                             sizeof(send_addr));
                }
                if (curr_ind == WINDOW_SIZE - 1) {
                  curr_ind = 0;
                } else {
                  curr_ind++;
                }
              } else {  // send all nack items
                for (int i = 0; i < nums_nack; i++) {
                  int nack_ind = mess_pac.nack[i] -
                                 win[curr_ind_zero].sequence + curr_ind_zero;
                  if (nack_ind >= WINDOW_SIZE) {
                    nack_ind -= WINDOW_SIZE;
                  }
                  sendto_dbg(ss, (char*)&win[nack_ind], sizeof(struct packet),
                             0, (struct sockaddr*)&send_addr,
                             sizeof(send_addr));
                }
              }
            } else if (offset == -1 &&
                       nums_nack != 0) {  // ack didn't change and there's nack
              for (int i = 0; i < nums_nack; i++) {
                int nack_ind = mess_pac.nack[i] - win[curr_ind_zero].sequence +
                               curr_ind_zero;
                if (nack_ind >= WINDOW_SIZE) {
                  nack_ind -= WINDOW_SIZE;
                }
                sendto_dbg(ss, (char*)&win[nack_ind], sizeof(struct packet), 0,
                           (struct sockaddr*)&send_addr, sizeof(send_addr));
              }
            } else {  // error otherwise
              perror("ncp: ack error");
              exit(1);
            }

            break;
          // receiver is busy
          case 2:
            sleep(10);  // sleep for 10 seconds and then come back
            break;
          case 3:  // receiver respond finished
            exit(0);
            break;
          default:
            perror("ncp: packet_ack tag error");
            exit(1);
        }
      }
      //   } else if (FD_ISSET(0, &read_mask)) {

      //     if (num_read > 0) {  // if read something
      //       printf("There is an packet: %s\n", pac);
      //       sendto_dbg(ss, pac, strlen(pac), 0, (struct sockaddr*)&send_addr,
      //                  sizeof(send_addr));
      //     }

      //     if (num_read < BUF_SIZE) {
      //       /* Did we reach the EOF? */
      //       if (feof(source_file)) {
      //         printf("Finished writing.\n");
      //         break;
      //       } else {
      //         printf("An error occurred...\n");
      //         exit(0);
      //       }
      //     }
      //   }
    } else {  // when timeout
      printf("timeout! \n");
      if (!begin) {
        sendto_dbg(ss, (char*)&start_packet, sizeof(struct packet), 0,
                   (struct sockaddr*)&send_addr, sizeof(send_addr));
      }
      if (finished) {
        temp_pac.tag = 3;
        sendto_dbg(ss, (char*)&temp_pac, sizeof(struct packet), 0,
                   (struct sockaddr*)&send_addr, sizeof(send_addr));
      } else {
        int last_ind =
            last_sequence - win[curr_ind_zero].sequence + curr_ind_zero;
        if (last_ind >= WINDOW_SIZE) {
          last_ind -= WINDOW_SIZE;
        }
        sendto_dbg(ss, (char*)&win[last_ind], sizeof(struct packet), 0,
                   (struct sockaddr*)&send_addr, sizeof(send_addr));
      }
      fflush(0);
    }
  }
}