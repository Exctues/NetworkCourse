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

#define PORT 2001
#define IP '127.0.0.1'
#define MAX_THREADS 10
#define BUFFER_SIZE 128
#define MAX_NEIGHBOURS 10

struct Node
{
    struct sockaddr_in address;
    struct Node *other_nodes;
};
struct Node known_nodes[MAX_NEIGHBOURS];

int THREAD_NBYTES[MAX_THREADS]; // 0 means that this thread is available and doing nothing. Other values means there is a job for the thread.
char THREAD_BUFFER[MAX_THREADS][BUFFER_SIZE];
int number_of_free_threads;
int sockfd;

void *thread_work(void *data)
{
    unsigned int thread_num = (unsigned int)data;
    while (1)
    {
        if (THREAD_NBYTES[thread_num] > 0)
        {
            printf("thread %d starting to process with \"%s\"\n", thread_num, THREAD_BUFFER[thread_num]);
            //fflush(stdin);
            sleep(1);
            printf("thread %d have done with string \"%s\"\n", thread_num, THREAD_BUFFER[thread_num]);
            //fflush(stdin);
            THREAD_NBYTES[thread_num] = 0; // work is done
            number_of_free_threads++;
        }
    }
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

void setup_node(){
    sockfd = socket(AF_INET, SOCK_STREAM, NULL);
    struct sockaddr_in server_addr, client_addr;

    if (sockfd == -1)
    {
        fprintf(stderr, "Socket() failed\n");
        exit(0);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(IP);
    server_addr.sin_port = htons(PORT);

    if ((bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) == -1)
    {
        fprintf(stderr, "Bind() failed\n");
        exit(0);
    }

    if ((listen(sockfd, 10)) < 0)
    {
        fprintf(stderr, "Listen() failed.\n");
        exit(0);
    }

    pthread_t threads[MAX_THREADS];
    for (long i = 0; i < MAX_THREADS; i++)
    {
        pthread_create(&threads[i], NULL, thread_work, (void *)i);
    }

    memset(THREAD_NBYTES, 0, sizeof THREAD_NBYTES); // 0 bytes means nothing to do for thread so it is free.
    number_of_free_threads = MAX_THREADS;

    fd_set readfds;
    //While loop which we use to sync our knowledge
    while (1)
    {
        while (number_of_free_threads <= 0)
        {
            printf("No available threads. \n");
            sleep(1);
        } // wait until at least one thread will be available

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        // Blocking call. Blocks until any data arrives to any of the FD's in readfds.

        select(sockfd + 1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(sockfd, &readfds))
        {
            printf("New connection recieved recvd, accept the connection. Client and Server completes TCP-3 way handshake at this point\n");

            // create descriptor which we use in order to communicate with new client.
            int common_sock_fd = accept(sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr));
            if (common_sock_fd < 0)
            {
                printf("accept error : errno = %d\n", errno);
                exit(0);
            }

            printf("Connection accepted from client : %s:%u\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }

        sleep(5); // Ping every 5 seconds
    }

    close(sockfd);
}

void main()
{
    signal(SIGINT, sig_handler);
    setup_node();
}