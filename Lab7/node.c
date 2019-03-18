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

#define PORT 2002
#define IP "192.168.31.105"
#define BUFFER_SIZE 128

#define MAX_NEIGHBOURS 32
#define MAX_NODE_NAME_LENGTH 32
#define MAX_FILE_NAME_LENGTH 64
#define MAX_THREADS 128
// my 192.168.31.105

struct Node {
    char name[MAX_NODE_NAME_LENGTH];
    struct sockaddr_in address;
};
struct thread_data {
    int sock_fd;
    struct sockaddr_in client_addr;
};

struct Node known_nodes[MAX_NEIGHBOURS];
int connection_with_node[MAX_NEIGHBOURS]; // 0 means we don't have open connection with this node. 1 means we have.
int known_nodes_exist[MAX_NEIGHBOURS];    // 0 means the slot is free, 1 means there is a node.
int size_known_nodes = 0;

int master_sockfd;
fd_set readfds;

// Info about myself
char myname[MAX_NODE_NAME_LENGTH];
struct Node our_node;


/** Compares two ip's. Returns 0 if ip's are equal and -1 otherwise*/
int addrcmp(struct sockaddr_in a, struct sockaddr_in b) {
    // TODO change passing whole struct to passing just pointer. Understand why there always different ports(but since it is 1 program we shouldn't care about port)
    char astr[32];
    char bstr[32];
    strcpy(astr, inet_ntoa(a.sin_addr));
    strcpy(bstr, inet_ntoa(b.sin_addr));
    if (strcmp(astr, bstr) == 0 && a.sin_port == b.sin_port) {
        return 0;
    }
    return -1;
}

/** Finds new free slot and adds new assembled node in that slot. Returns pos of new added Node*/
int add_known_node(struct Node node, int busy) {
    printf("I am going to add node with name %s\n", node.name);
    if (addrcmp(node.address, our_node.address) == 0) {
//        printf("Prevent to add myself.\n");
        return -1;
    }

    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (known_nodes_exist[i] == 0) {
            known_nodes[i] = node;
            connection_with_node[i] = busy;
            known_nodes_exist[i] = 1;
            size_known_nodes++;

            printf("Successfully added.\n");
            return i;
        }
    }

    printf("WARNING: No space to add new node.\n");
    return -1;
}

/** Search by node's address. Returns pos of that node and -1 if it doesnt exist*/
int find_known_node(struct Node node) {
    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (known_nodes_exist[i] == 1 && addrcmp(node.address, known_nodes[i].address)) {
            return i;
        }
    }

    return -1;
}


void sync_with(struct sockaddr_in to_connect) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    unsigned int len;
    if (sockfd == -1) {
        printf("ping_know_nodes: sockfd failed\n");
        exit(0);
    }

    if ((connect(sockfd, (struct sockaddr *) &to_connect, sizeof(to_connect))) != 0) {
        printf("ping_know_nodes: Connection with %s:%u failed\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
        close(sockfd);
        return;
    }

    printf("Connected with %s:%u successfully\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
    char *hello = "sync";
    sendto(sockfd, hello, strlen(hello), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    //1) Get their node
    struct Node their_node;
    recvfrom(sockfd, &their_node, sizeof(struct Node), 0, (struct sockaddr *) &to_connect, &len);

    // Check if we already know this node.
    int pos = find_known_node(their_node);

    //add this node as known
    if (pos == -1)
        pos = add_known_node(their_node, 1);
    else
        connection_with_node[pos] = 1;

    //2) send ourselves
    sendto(sockfd, &our_node, sizeof(struct Node), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    //3) Get n (number of nodes) their node knows.
    int k;
    recvfrom(sockfd, &k, sizeof(int), 0, (struct sockaddr *) &to_connect, &len);

    //4) get all nodes friend-node know, maybe here we can receive the whole Node structure.
    for (int i = 0; i < k; ++i) {
        struct Node tmp_node;
        recvfrom(sockfd, &tmp_node, sizeof(struct Node), 0, (struct sockaddr *) &to_connect, &len);
        if (find_known_node(their_node) == -1) {
            add_known_node(tmp_node, 0);
        }
    }

    //send number of nodes we are going to send
    int n = size_known_nodes - 1;
    sendto(sockfd, &k, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    //send all nodes
    for (int i = 0, sent = 0; i < MAX_NEIGHBOURS && sent < n; ++i) {
        if (known_nodes_exist[i] && i != pos) {
            sendto(sockfd, &known_nodes[i], sizeof(struct Node), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
            sent++;
        }
    }

    close(sockfd);
    if (pos >= 0) // we should mark that connection is closed
        connection_with_node[pos] = 0;

    printf("Socket to communicate with \"%s\" is closed.\n", their_node.name);
}

/** Do the job with given thread_data struct (socket, client_addr) */
void *thread_work(void *data) {
    struct thread_data *thread_data = data;
    int common_sock_fd = thread_data->sock_fd;
    struct sockaddr_in client_addr = thread_data->client_addr;

    ssize_t nbytes;
    unsigned int len;
    char *buffer = calloc(BUFFER_SIZE, sizeof(char)); // buffer with all zeroes initially.

    nbytes = recvfrom(common_sock_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &client_addr, &len);
    buffer[nbytes] = '\0';

    int pos = -1;
    struct Node their_node;
    if (strcmp(buffer, "sync") == 0) {
        printf("sync command received.\n");
        // 1) we send info about ourselves.
        sendto(common_sock_fd, &our_node, sizeof(struct Node), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));

        // 2) receive their info.
        recvfrom(common_sock_fd, &their_node, sizeof(struct Node), 0, (struct sockaddr *) &client_addr, &len);

        // Add received this node.
        pos = find_known_node(their_node);
        if (pos == -1)
            pos = add_known_node(their_node, 1);
        else
            connection_with_node[pos] = 1;

        // 3) we send n = # of nodes we know.
        int n = size_known_nodes - 1; // we need to send all nodes but not their's.
        sendto(common_sock_fd, &n, sizeof(int), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));

        // 4) we send n our nodes.
        for (int i = 0, sended = 0; i < MAX_NEIGHBOURS && sended < n; ++i) {
            if (known_nodes_exist[i] && pos != i) {
                sendto(common_sock_fd, &known_nodes[i], sizeof(struct Node), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
                sended++;
            }
        }

        // 5) we receive k = number of nodes this node knows.
        int k;
        recvfrom(common_sock_fd, &k, sizeof(int), 0, (struct sockaddr *) &client_addr, &len);

        // 6) we receive this k nodes.
        for (int i = 0; i < k; ++i) {
            struct Node tmp_node;
            recvfrom(common_sock_fd, &tmp_node, sizeof(struct Node), 0, (struct sockaddr *) &client_addr, &len);
            if (find_known_node(tmp_node) == -1) {
                add_known_node(tmp_node, 0);
            }
        }
    } else if (strcmp(buffer, "ping") == 0) {
        printf("ping command received\n");

    } else if (strcmp(buffer, "getFile") == 0) {
        printf("getFile received");
        // 1) Wait for the name of file
        char filename[MAX_FILE_NAME_LENGTH];
        recvfrom(common_sock_fd, &filename, MAX_FILE_NAME_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, &len);
        // 2) Check if we have such file
        // 3) If file exists then send number of words this file has and -1 otherwise
        // 4) send file word-by-word
    } else {
        printf("UNKNOWN COMMAND. I don't know what \"%s\" means..... HELP PLS.\n", buffer);
    }

    close(common_sock_fd);
    if (pos >= 0) { // we should mark that connection is closed
        printf("VSE OK OTKL!!!!\n");
        connection_with_node[pos] = 0;
    }
    if (strlen(their_node.name) > 1)
        printf("Socket to communicate with \"%s\" is closed.\n", their_node.name);
    else {
        printf("Socket to communicate is closed.\n");
    }

    free(data);
    free(buffer);
}

int ping(int i) {
    int tmp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tmp_socket == -1) {
        printf("ping_know_nodes, ping: tmp_socket failed\n");
        exit(0);
    }

    int need_exchange = 0;
    connection_with_node[i] = 1; // need to do something with this.
    char *ping = "ping";
    if ((connect(tmp_socket, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address))) == 0) {
        sendto(tmp_socket, ping, strlen(ping), 0, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address));
        printf("Node with name \"%s\" is still alive, its address is %s:%u.\n", known_nodes[i].name,
               inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));
    } else {
        printf("second chance\n");
        sleep(1);
        if ((connect(tmp_socket, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address))) == 0) {
            sendto(tmp_socket, ping, strlen(ping), 0, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address));
            printf("Node with name \"%s\" is still alive, its address is %s:%u.\n", known_nodes[i].name,
                   inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));
        } else {
            printf("Node with name \"%s\" is not alive anymore so we delete it its address is %s:%u.\n", known_nodes[i].name,
                   inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));

            known_nodes_exist[i] = 0; // delete this node
            size_known_nodes--;
        }
    }

    close(tmp_socket);
    connection_with_node[i] = 0;
    return need_exchange;
}

/** Checks all known nodes if they are still live. Node runs thread to do this job before entering main loop*/
void *ping_know_nodes(void *data) {
    //TODO inside loop add: If we don't know any node but know, from arguments, where we can connect then connect.
    printf("ping_know_nodes() started.\n");
    while (1) {
        printf("size of known nodes is %d\n", size_known_nodes);
        for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
            if (known_nodes_exist[i] == 1) { // 1 means there is a node
                if (connection_with_node[i] == 0) { // 0 means there is no connection right now.
                    //ping it
                    int need_exchange = ping(i);
                    if (need_exchange == 1) {
                        printf("Let's exchange\n");
                        sync_with(known_nodes[i].address);
                    }
                } else {
                    printf("I am still connected with \"%s\".\n", known_nodes[i].name);
                }
            }
        }
        sleep(8);
    }
}

void setup_node(int argc, char **argv) {

    master_sockfd = socket(AF_INET, SOCK_STREAM, 0); // master socket. Used as entrance point only.

    if (master_sockfd == -1) {
        fprintf(stderr, "Socket() failed\n");
        exit(0);
    }
    int reuse = 1;
    if (setsockopt(master_sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    strcpy(our_node.name, argv[1]); // set our name
    //set our address family, ip, port
    memset(&our_node.address, 0, sizeof(struct sockaddr_in));
    our_node.address.sin_family = AF_INET;
    our_node.address.sin_addr.s_addr = inet_addr(IP);
    our_node.address.sin_port = htons(PORT);


    if ((bind(master_sockfd, (struct sockaddr *) &our_node.address, sizeof(struct sockaddr_in))) == -1) {
        fprintf(stderr, "Bind() failed\n");
        exit(0);
    }
    if ((listen(master_sockfd, 10)) < 0) {
        fprintf(stderr, "Listen() failed.\n");
        exit(0);
    }

    if (argc == 4) { // need to connect to some entrance point first
        struct sockaddr_in to_connect;
        to_connect.sin_family = AF_INET;
        to_connect.sin_addr.s_addr = inet_addr(argv[2]);
        to_connect.sin_port = htons((uint16_t) atoi(argv[3]));

        printf("I am going to sync with entrance point %s:%u\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

        //TODO finish exchange.
        sync_with(to_connect);
    }

    pthread_t availability_check_thread;

    pthread_create(&availability_check_thread, NULL, ping_know_nodes, NULL);
    pthread_t pthreads[MAX_THREADS];
    unsigned int next_thread = 0;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(master_sockfd, &readfds);

        printf("I am waiting on select\n");
        select(master_sockfd + 1, &readfds, NULL, NULL, NULL); //Blocks until any data arrives to any of the FD's in readfds.

        if (FD_ISSET(master_sockfd, &readfds)) {
            struct sockaddr_in client_addr;
            memset(&client_addr, 0, sizeof(client_addr));

            unsigned int addr_len = sizeof(struct sockaddr);
            int common_sock_fd = accept(master_sockfd, (struct sockaddr *) &client_addr, &addr_len);
            if (common_sock_fd < 0) {
                printf("accept error : errno = %d\n", errno);
                exit(0);
            }

            printf("Connection accepted from node: %s:%u\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            struct thread_data *thread_data = malloc(sizeof(thread_data));
            memset(thread_data, 0, sizeof(struct thread_data)); // debug
            thread_data->sock_fd = common_sock_fd;
            thread_data->client_addr = client_addr;

            pthread_create(&pthreads[next_thread], NULL, thread_work, (void *) thread_data);
            next_thread++;
            if (next_thread >= MAX_THREADS)
                next_thread = 0;
        }
    }
}

/** ctrl+c is the only way to exit correctly */
void sig_handler(int signo) {
    if (signo == SIGINT) {
        printf("received SIGINT. Exiting...\n");
    }
    close(master_sockfd);
    exit(1);
}

/**
 * Always run with name as argument. Then ip and port if you want to connect to somewhere first.
 * Example:
 * 1) ./node.out lol.
 * 2) ./node.out kek 192.168.0.1 2000
 * */
int main(int argc, char **argv) {
    if (argc == 1 || argc == 3 || argc > 4) {
        printf("Number of arguments must be either 1 or 3. Program exiting ... \n");
        exit(0);
    }
    memset(known_nodes_exist, 0, sizeof(int) * MAX_NEIGHBOURS);
    memset(connection_with_node, 0, sizeof(int) * MAX_NEIGHBOURS);
    strcpy(myname, argv[1]);
    signal(SIGINT, sig_handler);
    setup_node(argc, argv);
    return 0;
}