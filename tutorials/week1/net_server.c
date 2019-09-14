#include "net_include.h"

int main()
{
    struct sockaddr_in name;
    int                s;
    int                recv_s[10];
    int                valid[10];  
    fd_set             mask;
    fd_set             read_mask, write_mask, excep_mask;
    int                i,j,num;
    int                mess_len;
    int                neto_len;
    char               mess_buf[MAX_MESS_LEN];
    long               on=1;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s<0) {
        perror("Net_server: socket");
        exit(1);
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
    {
        perror("Net_server: setsockopt error \n");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    if ( bind( s, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Net_server: bind");
        exit(1);
    }
 
    if (listen(s, 4) < 0) {
        perror("Net_server: listen");
        exit(1);
    }

    i = 0;
    FD_ZERO(&mask);
    FD_ZERO(&write_mask);
    FD_ZERO(&excep_mask);
    FD_SET(s,&mask);
    for(;;)
    {
        read_mask = mask;
        num = select( FD_SETSIZE, &read_mask, &write_mask, &excep_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET(s,&read_mask) ) {
                recv_s[i] = accept(s, 0, 0) ;
                FD_SET(recv_s[i], &mask);
                valid[i] = 1;
                i++;
            }
            for(j=0; j<i ; j++)
            {   if (valid[j])    
                if ( FD_ISSET(recv_s[j],&read_mask) ) {
                    if( recv(recv_s[j],&mess_len,sizeof(mess_len),0) > 0) {
                        neto_len = mess_len - sizeof(mess_len);
                        recv(recv_s[j], mess_buf, neto_len, 0 );
                        mess_buf[neto_len] = '\0';
                    
                        printf("socket is %d ",j);
                        printf("len is :%d  message is : %s \n ",
                               mess_len,mess_buf); 
                        printf("---------------- \n");
                    }
                    else
                    {
                        printf("closing %d \n",j);
                        FD_CLR(recv_s[j], &mask);
                        close(recv_s[j]);
                        valid[j] = 0;  
                    }
                }
            }
        }
    }

    return 0;

}

