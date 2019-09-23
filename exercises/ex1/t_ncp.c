#include "net_include.h"
#include "helper.h"
#include "packet.h"

int main(int argc, char** argv)
{
    struct sockaddr_in host;
    struct hostent     h_ent, *p_h_ent;

    int                s;
    int                ret;
    
    if (argc != 3) {
        printf("t_ncp usage: ncp <source_file_name> "
                "<dest_file_name>@<comp_name>");
        exit(0);
    }

    // extract computer name and destination file name from args
    char comp_name[8] = {'\0'};
    char* temp = argv[2];
    int comp_char_index = 0;
    bool has_at = false;

    char filename[MAX_PACKET_SIZE - sizeof(int)];
    int filename_len;

    // parse filename and rcv name from command line
    for (int i = 0; i < strlen(temp); i++) {
        if ((!has_at) && temp[i] != '@') {
            // put filename to packet
            filename[i] = temp[i];
            // record size of filename in packet
            filename_len++;

            if (filename_len > MAX_PACKET_SIZE - sizeof(int)) {
                printf("T_ncp: filename is too long\n");
                exit(0);
            }

        } else if ((!has_at) && temp[i] == '@') {
            has_at = true;
        } else if (has_at) {
            if (comp_char_index == 7) {
                perror("t_ncp invalid command: <comp_name> is too long\n");
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

    s = socket(AF_INET, SOCK_STREAM, 0); /* Create a socket (TCP) */
    if (s<0) {
        perror("T_ncp: socket error");
        exit(1);
    }

    host.sin_family = AF_INET;
    host.sin_port   = htons(PORT);


    p_h_ent = gethostbyname(comp_name);
    if ( p_h_ent == NULL ) {
        printf("T_ncp: gethostbyname error.\n");
        exit(1);
    }

    memcpy( &h_ent, p_h_ent, sizeof(h_ent) );
    memcpy( &host.sin_addr, h_ent.h_addr_list[0],  sizeof(host.sin_addr) );

    ret = connect(s, (struct sockaddr *)&host, sizeof(host) ); /* Connect! */
    if (ret < 0) {
        perror( "T_ncp: could not connect to server"); 
        exit(1);
    }

    // record starting time of transfer   
    struct timeval start_time;
    struct timeval last_time;

    FILE* source_file;  // pointer to source file
    if ((source_file = fopen(argv[1], "r")) == NULL) {
        perror("ncp: fopen");
        exit(1);
    }

    // Send filename to rcv
    char buffer[PACKET_SIZE];
    ret = send(s, &filename_len, sizeof(int), 0);
    if (ret != sizeof(int)) {
        printf("Warning: does NOT send filename_len\n");
    }
    ret = send(s, &filename, filename_len, 0);
    
    gettimeofday(&start_time, NULL);
    gettimeofday(&last_time, NULL);
    printf("----------------START-----------------\n");

    printf("Start transfer file %s\n", argv[1]);
    if (ret != filename_len) {
        printf("Warning: does NOT send filename\n");
    }

    bool last_packet = false;

    unsigned int bytes = 0;
    for(;;)
    {
        int read_bytes = fread(&buffer, 1, MAX_PACKET_SIZE, source_file);
        if (read_bytes < MAX_PACKET_SIZE) {
            if (feof(source_file)) {  // when we reach the EOF
                last_packet = true;
            } else {
                printf("An error occurred when finished reading...\n");
                exit(0);
            }
        }

        ret = send( s, &buffer, read_bytes, 0);
        if(ret != read_bytes)
        {
            perror( "T_ncp: error in writing");
            exit(1);
        }

        unsigned int old_bytes = bytes;
        bytes += ret;

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

        if (last_packet) {
            
            struct timeval end_time;
            gettimeofday(&end_time, NULL);
            struct timeval diff_time = diffTime(end_time, start_time);
            double seconds = diff_time.tv_sec + ((double) diff_time.tv_usec) / 1000000;
            
            printf("Report: Size of file transferred is %.2f Mbytes\n", (double) bytes / (1024 * 1024));
            printf("        Amount of time spent is %.2f seconds\n", seconds);
            printf("        Average rate is %.2f Mbits/sec\n",
                                    (double) bytes / (1024 * 1024) * 8 / seconds);
            printf("-----------------END------------------\n");
        
            exit(0);
        }

    }

    return 0;

}


