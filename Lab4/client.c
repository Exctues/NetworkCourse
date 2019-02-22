#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <memory.h>
#include <unistd.h>
#include "common.h"

#define SERVER_PORT 2001
#define SERVER_IP_ADDRESS "127.0.0.1"
#define SEND_TIMES 4

test_struct_t client_data;
result_struct_t result;

void setup_udp_communication()
{
    int sockfd = 0;
    char hello[] = "Hello from client ";

    struct sockaddr_in servaddr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        fprintf(stderr, "Client: socket wrong\n");
        exit(1);
    }
    
    memset(&servaddr, 0, sizeof servaddr);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);
    servaddr.sin_port = SERVER_PORT;

    
    int wordlen = strlen(hello);
    for(int i = 0; i < SEND_TIMES; i++)
    {
        hello[wordlen - 1] = (char)((int)'1' + i);    
        int nbytes = sendto(sockfd, hello, wordlen, 0, (struct sockaddr *)&servaddr, sizeof servaddr);
        printf("Client have sent %d bytes.\n", nbytes);
    }
    
    
    close(sockfd);
}

int main(int argc, char **argv)
{
    setup_udp_communication();
    printf("Client quits\n");
    return 0;
}