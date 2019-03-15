#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <memory.h>
#include <errno.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

// MAX_THREADS should be allocated dynamicaly but for this task let me simply ignore packets which are late.
#define SERVER_PORT 2001
#define MAX_THREADS 5
#define BUFFER_SIZE 128

int sockfd = 0;

int THREAD_NBYTES[MAX_THREADS]; // 0 means that this thread is available and doing nothing. Other values means there is a job for the thread.
char THREAD_BUFFER[MAX_THREADS][BUFFER_SIZE];
int number_of_free_threads;

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
        sleep(0.2); 
    }
}
/** 
 * returns number of first free thread if there any otherwise (if all are busy) -1.
*/
int get_next_free_thread()
{
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (THREAD_NBYTES[i] == 0)
        {
            return i;
        }
    }

    return -1;
}

void setup_udp_server_communication()
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        fprintf(stderr, "Server: socket wrong\n");
        exit(1);
    }

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
        pthread_create(&threads[i], NULL, thread_work, (void *)i);
    }

    memset(THREAD_NBYTES, 0, sizeof THREAD_NBYTES); // 0 bytes means nothing to do for thread so it is free.
    number_of_free_threads = MAX_THREADS;

    while (1)
    {
        while (number_of_free_threads <= 0)
        {
            printf("No available threads. \n");
            sleep(1);
        } // wait until at least one thread will be available
    
        int next_thread = get_next_free_thread();
        int nbytes, len;
        printf("next available thread is %d \n", next_thread);
        printf("Server: waiting to recvfrom...\n");
        nbytes = recvfrom(sockfd, (char *)(&THREAD_BUFFER[next_thread]), BUFFER_SIZE, 0, (struct sockaddr *)&clientaddr, &len);
        printf("# of free threads: %d \n", number_of_free_threads);
        
        number_of_free_threads--;
        THREAD_BUFFER[next_thread][nbytes] = '\0';
        THREAD_NBYTES[next_thread] = nbytes;
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
