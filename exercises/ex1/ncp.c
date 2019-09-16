#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "sendto_dbg.h"

#define WIN_SIZE 100;
#define PAC_SIZE 10;

int main(int argc, char** argv) {
  FILE* source;  // pointer to source file
  char win[WIN_SIZE];
  char pac[PAC_SIZE];
  struct timeval timeout;
  int num_read;

  if (argc != 4) {
    printf(
        "Usage: ncp <loss_rate_percent> <source_file_name> "
        "<dest_file_name>@<comp_name>");
    exit(0);
  }

  if ((source = fopen(argv[2], "r")) == NULL) {
    perror("fopen");
    exit(0);
  }

  printf("Opened %s for reading...\n", argv[2]);

  for (;;) {
    num_read = fread(win, 1, WIN_SIZE, source);
  }
}