#include "net_include.h"
#include "sendto_dbg.h"

#define WIN_SIZE 100
#define PAC_SIZE 10
#define NAME_LENGTH 80

int main(int argc, char** argv) {
  struct sockaddr_in name;
  struct sockaddr_in send_addr;
  struct sockaddr_in from_addr;
  socklen_t from_len;
  struct hostent h_ent;
  struct hostent* p_h_ent;
  char comp_name[NAME_LENGTH] = {'\0'};
  char my_name[NAME_LENGTH] = {'\0'};
  int host_num;
  int from_ip;
  int ss, sr;
  fd_set mask;
  fd_set read_mask, write_mask, excep_mask;
  int bytes;
  int num;
  char mess_buf[MAX_MESS_LEN];
  char dest_file_name[NAME_LENGTH] = {'\0'};

  FILE* dest_file;                // pointer to destnation file
  char win[WIN_SIZE * PAC_SIZE];  // not sure about it
  char pac[PAC_SIZE];
  struct timeval timeout;
  int num_written;

  sendto_dbg_init(0);

  // args error checking
  if (argc != 2) {
    printf("Usage: rcv <loss_rate_percent>");
    exit(0);
  }

  // socket for receiving
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

  for (;;) {
    read_mask = mask;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    num = select(FD_SETSIZE, &read_mask, &write_mask, &excep_mask, &timeout);
    if (num > 0) {
      if (FD_ISSET(sr, &read_mask)) {
        from_len = sizeof(from_addr);
        bytes = recvfrom(sr, pac, sizeof(pac), 0, (struct sockaddr*)&from_addr,
                         &from_len);
        mess_buf[bytes] = 0;
        from_ip = from_addr.sin_addr.s_addr;
        num_written = fwrite(pac, 1, PAC_SIZE, "hardcoded_dest_file");
        printf("Received message from (%d.%d.%d.%d): %s\n",
               (htonl(from_ip) & 0xff000000) >> 24,
               (htonl(from_ip) & 0x00ff0000) >> 16,
               (htonl(from_ip) & 0x0000ff00) >> 8,
               (htonl(from_ip) & 0x000000ff), pac);

      } else if (FD_ISSET(0, &read_mask)) {
        sendto_dbg(ss, "A hardcoded message", 20, 0,
                   (struct sockaddr*)&send_addr, sizeof(send_addr));
        // if (num_read > 0) {  // if read something
        //   printf("There is an packet: %s\n", pac);
        //   sendto_dbg(ss, pac, strlen(pac), 0, (struct sockaddr*)&send_addr,
        //              sizeof(send_addr));
        // }
        // if (num_read < PAC_SIZE) {
        //   /* Did we reach the EOF? */
        //   if (feof(source_file)) {
        //     printf("Finished writing.\n");
        //     break;
        //   } else {
        //     printf("An error occurred...\n");
        //     exit(0);
        //   }
        // }
      }
    } else {
      printf(".");
      fflush(0);
    }
  }
}