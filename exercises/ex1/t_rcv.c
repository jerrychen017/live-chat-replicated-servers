#include "net_include.h"
#include "t_packet.h"
#include "helper.h"

int main()
{
    struct sockaddr_in name;
    int                s;
    int                recv_s;
    fd_set             mask;
    fd_set             read_mask;
    long               on = 1;
    int                num;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s<0) {
        perror("T_rcv: socket");
        exit(1);
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
    {
        perror("T_rcv: setsockopt error \n");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    if ( bind( s, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("T_rcv: bind");
        exit(1);
    }
 
    if (listen(s, 4) < 0) {
        perror("T_rcv: listen");
        exit(1);
    }

    FD_ZERO(&mask);
    FD_SET(s,&mask);
    
    struct t_packet packet; 
    bool busy = false;

    FILE* fw;

    // record starting time of transfer   
    struct timeval start_time;
    struct timeval last_time;
    unsigned int bytes = 0;

    bool first_packet = true;
    int filename_len = 0;
    int received_bytes = 0;

    char buffer[MAX_PACKET_SIZE];

    for(;;)
    {
        read_mask = mask;
        num = select( FD_SETSIZE, &read_mask, NULL, NULL, NULL);
        if (num > 0) {
            if ( FD_ISSET(s, &read_mask) ) {
                if (busy) {
                    printf("Notice: Another sender wants to connect, but busy\n");
                }
                recv_s = accept(s, 0, 0) ;
                FD_SET(recv_s, &mask);
                busy = true;
                printf("Establish connection with sender\n");
                gettimeofday(&start_time, NULL);
                gettimeofday(&last_time, NULL);
            }
            if ( FD_ISSET(recv_s,&read_mask) ) {
                if (first_packet) {
                    received_bytes = recv(recv_s, &filename_len, sizeof(int), 0);
                    if (received_bytes != sizeof(int)) {
                        printf("Warning: does NOT get the length of filename");
                    }
                    char filename[filename_len + 1];
                    received_bytes = recv(recv_s, &filename, filename_len, 0);
                    if (received_bytes != filename_len) {
                        printf("Warning: does NOT get the filename");
                    }
                    filename[filename_len] = '\0';
                    printf("Start transfer file filename %s\n", filename);
                    fw = fopen(filename, "w");
                    if (fw == NULL) {
                        perror("T_rcv: cannot open or create file for writing");
                        exit(1);
                    }
                    busy = true;
                    first_packet = false;        
                    // record starting time
                    gettimeofday(&start_time, NULL);
                    gettimeofday(&last_time, NULL);

                    printf("----------------START-----------------\n");
                    printf("Start transfer file %s\n", filename);
                
                } else if ((received_bytes = recv(recv_s, &buffer, MAX_PACKET_SIZE, 0)) > 0) {
                
                    int bytes_written = fwrite(buffer, 1, received_bytes, fw);
                    if (bytes_written != received_bytes) {
                        printf("Warning: write to file less than %d bytes", packet.bytes);
                    }
                    
                    unsigned int old_bytes = bytes;
                    bytes += bytes_written;
                    // report statistics every 100 MBytes
                    if (old_bytes / 100000000 != bytes / 100000000) {
                        struct timeval current_time;
                        gettimeofday(&current_time, NULL);
                        struct timeval diff_time = diffTime(current_time, last_time);
                        double seconds = diff_time.tv_sec + ((double) diff_time.tv_usec) / 1000000;
                        printf("Report: total amount of data transferred is %u Mbytes\n", bytes / (1024 * 1024));
                        printf("        average transfer rate of the last 100 Mbytes received is %.2f Mbits/sec\n", (double) (100) * 8 / seconds);
                        last_time = current_time;
                    } 
                    
                }
                else
                {
                    struct timeval end_time;
                    gettimeofday(&end_time, NULL);
                    struct timeval diff_time = diffTime(end_time, start_time);
                    double seconds = diff_time.tv_sec + ((double) diff_time.tv_usec) / 1000000;
                    printf("Report: Size of file transferred is %.2f Mbytes\n", (double) bytes / (1024 * 1024));
                    printf("        Amount of time spent is %.2f seconds\n", seconds);
                    printf("        Average rate is %.2f Mbits/sec\n",
                                    (double) bytes / (1024 * 1024) * 8 / seconds);
                    printf("-----------------END------------------\n");
                    
                    FD_CLR(recv_s, &mask);
                    close(recv_s);
                    fclose(fw);

                    exit(0);
                
                }
            }
        }
    }

    return 0;

}

