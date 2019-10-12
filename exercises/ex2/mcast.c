#include "net_include.h"

int main() {

    // create socket for both sending and receiving
    struct sockaddr_in my_address;
    int sk = socket(AF_INET, SOCK_DGRAM, 0); /* socket */
    if (sk<0) {
        perror("Mcast: socket");
        exit(1);
    }

    my_address.sin_family = AF_INET;
    my_address.sin_addr.s_addr = INADDR_ANY;
    my_address.sin_port = htons(PORT);

    if ( bind( sk, (struct sockaddr *)&my_address, sizeof(my_address) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    struct ip_mreq     mreq;
    int mcast_addr = 225 << 24 | 1 << 16 | 1 << 8 | 100; /* (225.1.1.100) */
    mreq.imr_multiaddr.s_addr = htonl( mcast_addr );
    /* the interface could be changed to a specific interface if needed */
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    // join multicast group
    if (setsockopt(sk, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, 
        sizeof(mreq)) < 0) {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    unsigned char ttl_val = 1;
    if (setsockopt(sk, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, 
        sizeof(ttl_val)) < 0) {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val );
    }

    struct sockaddr_in send_addr;
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                num;

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sk, &mask );

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;

    for(;;) {
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, NULL, NULL, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sk, &temp_mask) ) {
                
            } else {
                // timeout

            }
        }
    }

    return 0;

}