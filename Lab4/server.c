//Taken from Abhishek Sagar

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <memory.h>
#include <errno.h>
#include <sys/select.h>
#include "common.h"
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#define SERVER_PORT 2001
#define MAX_THREADS 10
#define BUFFER_SIZE 256

// test_struct_t test_struct;
// result_struct_t res_struct;

int sockfd = 0;

char THREAD_BUFFER[MAX_THREADS][BUFFER_SIZE]; // there is buffer for each thread; [X][BUFFER_SIZE-1] == 0  means nothing to do.

void *thread_work(void *data)
{
    
    unsigned int thread_num = (unsigned int)data;
    while (1)
    {
        if (THREAD_BUFFER[thread_num][BUFFER_SIZE - 1] > 0)
        {
            printf("thread %d have processed with string \"%s\"\n", thread_num, THREAD_BUFFER[thread_num]);
            THREAD_BUFFER[thread_num][BUFFER_SIZE - 1] = 0; // work is done
        }
    }
}

void setup_udp_server_communication()
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        fprintf(stderr, "socket wrong\n");
        exit(1);
    }

    char *msgto_client = "Hello form server";
    struct sockaddr_in servaddr, clientaddr;

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&clientaddr, 0, sizeof(clientaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = SERVER_PORT;

    if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof servaddr)) == -1)
    {
        fprintf(stderr, "Server: bind() is not working\n");
        close(sockfd);
        exit(1);
    }

    pthread_t threads[MAX_THREADS];
    for (long i = 0; i < MAX_THREADS; i++)
    {
        pthread_create(&threads[i], NULL, thread_work, (void *) i);
    }

    int nrec = 0; // number of received packets
    while (1)
    {
        printf("Server: waiting to recvfrom...\n");

        int nbytes, len;
        int next_thread = nrec % MAX_THREADS;
        nbytes = recvfrom(sockfd, (char *)(THREAD_BUFFER[nrec % MAX_THREADS]), BUFFER_SIZE, 0, (struct sockaddr *)&clientaddr, &len);

        THREAD_BUFFER[next_thread][nbytes] = '\0';
        THREAD_BUFFER[next_thread][BUFFER_SIZE - 1] = nbytes;
        nrec++;
    }
    close(sockfd);
}

void sig_handler(int signo)
{
    if (signo == SIGINT)
    {
        printf("received SIGINT. Exiting...\n");
    }
    close(sockfd);
    exit(1);
}

int main(int argc, char **argv)
{
    signal(SIGINT, sig_handler);
    setup_udp_server_communication();
    return 0;
}
