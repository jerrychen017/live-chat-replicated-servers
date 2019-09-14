#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "sendto_dbg.h"

int main(int argc, char **argv)
{
  int rate, s, i;
  char buf[6] = "Hello";
  char *dest;
  struct sockaddr_in to_addr;
  struct hostent h_ent;
  struct hostent *p_h_ent;
  long host_num;

  if(argc != 3) {
    printf("Usage: ./test loss_rate dest_machine\n");
    exit(0);
  }
    
  rate = atoi(argv[1]);
  dest = (char *)argv[2];

  /* Call this once to initialize the coat routine */
  sendto_dbg_init(rate);

  /* Create a socket for sending messages to ucast */
  s = socket(AF_INET, SOCK_DGRAM, 0);  
  if(s < 0) {
    perror("Ucast: socket");
    exit(1);
  }

  to_addr.sin_family      = AF_INET; 
  to_addr.sin_port        = htons(5555);

  /* Get the IP address of the destination machine */
  p_h_ent = gethostbyname(dest);
  if(p_h_ent == NULL ) {
    printf("Ucast: gethostbyname error.\n");
    exit(1);
  }

  memcpy( &h_ent, p_h_ent, sizeof(h_ent));
  memcpy( &host_num, h_ent.h_addr_list[0], sizeof(host_num) );

  to_addr.sin_addr.s_addr = host_num;
  
  /* Try to send the message 10 times -- how many will actually be sent? */
  for(i = 0; i < 10; i++) {
    sendto_dbg(s, buf, 6, 0, (struct sockaddr *)&to_addr, 
               sizeof(to_addr));
  }
  
  return 0;
}
