#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <memory.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>

#define SYNC 1

void sync_with(const struct sockaddr_in to_connect) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    size_t nbytes;
    if (sockfd == -1) {
        printf("sync_with_known_nodes(): sockfd failed\n");
        exit(0);
    }

    if ((connect(sockfd, (struct sockaddr *) &to_connect, sizeof(to_connect))) != 0) {
        printf("sync_with_known_nodes(): Connection with %s:%u failed\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
        //it would be better to delete known nodes if they are dead. But for the sake of simplicity and performance(no-sync) ability let it be.
        close(sockfd);
        return;
    }


    int msg = 1;
    nbytes = sendto(sockfd, &msg, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    if (nbytes == 0) {
        close(sockfd);
        return;
    }


    close(sockfd);
}

int main(int argc, char **argv){
    if(argc != 3){
        printf("\nShould be 2 arguments: 'ip' 'port'\n");
    }
    struct sockaddr_in to_connect;
    to_connect.sin_family = AF_INET;
    to_connect.sin_addr.s_addr = inet_addr(argv[2]);
    to_connect.sin_port = htons((uint16_t) atoi(argv[3]));


    while(1){
        sync_with(to_connect);
        usleep(100);
    }

    return 0;
}

