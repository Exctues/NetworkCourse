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
#define MAX_NAME_LENGTH 32
#define MAX_THREADS 64
// my 192.168.31.105
// friend 192.168.31.64
struct Node {
    char name[MAX_NAME_LENGTH];
    struct sockaddr_in address;
};
struct thread_data {
    int sock_fd;
    struct sockaddr_in client_addr;
};

struct Node known_nodes[MAX_NEIGHBOURS];
int connection_with_node[MAX_NEIGHBOURS]; // means we don't have open connection with this node. 1 means we have.
int known_nodes_exist[MAX_NEIGHBOURS]; // 1 means there is a node, 0 means the slot is free.
int size_known_nodes = 0;

int master_sockfd;
fd_set readfds;

// Info about myself
char myname[MAX_NAME_LENGTH];
struct sockaddr_in our_addr;

// TODO change passing whole struct to passing just pointer. Understand why there always different ports(but since it is 1 program we shouldn't care about port)
/** Compares two ip's. Returns 0 if ip's are equal and -1 otherwise*/
int addrcmp(struct sockaddr_in a, struct sockaddr_in b) {
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
int add_known_node(char name[MAX_NAME_LENGTH], struct sockaddr_in address, int busy) {
//    printf("FIRST TRY our = %s with friend'уs = %s \n", inet_ntoa(our_addr.sin_addr), inet_ntoa(address.sin_addr));
//    printf("SECOND TRY our = %s with friend's = %s \n", inet_ntoa(address.sin_addr), inet_ntoa(our_addr.sin_addr));
    printf("I am going to add node with name %s\n", name);
    printf("Going to cmp our = %s ", inet_ntoa(our_addr.sin_addr));
    printf("with friend's = %s\n", inet_ntoa(address.sin_addr));
    if (addrcmp(our_addr, address) == 0) {
        printf("Prevent to add myself.\n");
        return -1;
    }

    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (known_nodes_exist[i] == 0) {
            strcpy(known_nodes[i].name, name);
            known_nodes[i].address = address;
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

/** Search by address. Returns pos of that node and -1 if it doesnt exist*/
int find_known_node(struct sockaddr_in address) {
    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (known_nodes_exist[i] == 1 && addrcmp(address, known_nodes[i].address)) {
            return i;
        }
    }
    return -1;
}

void exchange_info_with(struct sockaddr_in to_connect) {
    int tmp_socket = socket(AF_INET, SOCK_STREAM, 0);
    unsigned int n, len;
    if (tmp_socket == -1) {
        printf("check_available: tmp_socket failed\n");
        exit(0);
    }

    if ((connect(tmp_socket, (struct sockaddr *) &to_connect, sizeof(to_connect))) != 0) {
        printf("Connection with %s:%u failed\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
        close(tmp_socket);
        return;
        exit(0);// in case you want to exit on fail.
    }

    int pos = find_known_node(to_connect);
    if (pos >= 0) {
        connection_with_node[pos] = 1;
    }

    printf("Connected with %s:%u successfully\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
    char *hello = "exchange";
    sendto(tmp_socket, hello, strlen(hello), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    //1) Get the name
    ssize_t nbytes;
    char name[MAX_NAME_LENGTH];
    memset(name, 0, sizeof(char) * MAX_NAME_LENGTH);

    nbytes = recvfrom(tmp_socket, &name[0], sizeof(char) * MAX_NAME_LENGTH, 0, (struct sockaddr *) &to_connect, &len);
    name[nbytes] = '\0';
    printf("Received name is %s\n", name);
    //add this node as known
    if (pos == -1)
        pos = add_known_node(name, to_connect, 1);
    //2) send name
    sendto(tmp_socket, myname, strlen(myname), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    //3) Get n = number of nodes our friend-node know.
    recvfrom(tmp_socket, &n, sizeof(int), 0, (struct sockaddr *) &to_connect, &len);
    printf("Received!! n = number of nodes of \"%s\" n = %d\n", name, n);

    //4) get all nodes friend-node know, maybe here we can receive the whole Node structure.
    for (int i = 0; i < n; ++i) {
        char cur_node_name[MAX_NAME_LENGTH];
        struct sockaddr_in cur_node_sockaddr_in;
        recvfrom(tmp_socket, &cur_node_name, sizeof(cur_node_name), 0, (struct sockaddr *) &to_connect, &len);
        recvfrom(tmp_socket, &cur_node_sockaddr_in, sizeof(cur_node_sockaddr_in), 0, (struct sockaddr *) &to_connect, &len);
        add_known_node(cur_node_name, cur_node_sockaddr_in, 0);
    }
    //TODO here we need sync;
    //send number of nodes we are going to send
    int k = size_known_nodes - 1;
    sendto(tmp_socket, &k, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    //send all nodes
    for (int i = 0, sent = 0; i < MAX_NEIGHBOURS && sent < n; ++i) {
        if (known_nodes_exist[i] && addrcmp(known_nodes[i].address, to_connect) != 0) {
            // send name
            sendto(tmp_socket, &known_nodes[i].name, sizeof(known_nodes[i].name), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
            // send address_in
            sendto(tmp_socket, &known_nodes[i].address, sizeof(known_nodes[i].address), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
            sent++;
        }
    }


    close(tmp_socket);
    if (pos >= 0)
        connection_with_node[pos] = 0;

    printf("Connection is closed with \"%s\"\n", name);
}

/** Do the job with given thread_data struct (socket, client_addr) */
void *thread_work(void *data) {
    struct thread_data *thread_data = data;
    int common_sock_fd = thread_data->sock_fd;
    struct sockaddr_in client_addr = thread_data->client_addr;

    int pos = find_known_node(client_addr);
    if (pos >= 0) {
        connection_with_node[pos] = 1;
    }

    ssize_t nbytes;
    unsigned int len;
    char *buffer = calloc(BUFFER_SIZE, sizeof(char)); // buffer with all zeroes initially.

    nbytes = recvfrom(common_sock_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &client_addr, &len);
    buffer[nbytes] = '\0';

    if (strcmp(buffer, "exchange") == 0) {
        printf("exchange command received.\n");
        // 1) we send our name
        sendto(common_sock_fd, &myname[0], sizeof(myname), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
        printf("Sent ourname = \"%s\".\n", myname);

        // 2) we receive its name.
        char name[32];
        nbytes = recvfrom(common_sock_fd, &name[0], sizeof(name), 0, (struct sockaddr *) &client_addr, &len);
        name[nbytes] = '\0';
        printf("Received name is \"%s\".\n", name);

        // Add this node
        if (pos == -1)
            pos = add_known_node(name, client_addr, 1);
        int n = size_known_nodes - 1; // we need to send all nods but not friend's info for himself.

        // 3) we send n = # of nodes we know.
        sendto(common_sock_fd, &n, sizeof(int), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));

        // 4) we send n our nodes.
        for (int i = 0, sended = 0; i < MAX_NEIGHBOURS && sended < n; ++i) {
            if (known_nodes_exist[i] && addrcmp(known_nodes[i].address, client_addr) != 0) {
                // send name
                sendto(common_sock_fd, &known_nodes[i].name, sizeof(known_nodes[i].name), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
                // send address_in
                sendto(common_sock_fd, &known_nodes[i].address, sizeof(known_nodes[i].address), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
                sended++;
            }
        }

        // 5) we receive k = number of nodes this node knows.
        int k;
        recvfrom(common_sock_fd, &k, sizeof(int), 0, (struct sockaddr *) &client_addr, &len);

        // 6) we receive this k nodes.
        for (int i = 0; i < k; ++i) {
            char cur_node_name[MAX_NAME_LENGTH];
            struct sockaddr_in cur_node_sockaddr_in;
            // send name
            recvfrom(common_sock_fd, &cur_node_name, sizeof(cur_node_name), 0, (struct sockaddr *) &client_addr, &len);

            // receive address_in
            recvfrom(common_sock_fd, &cur_node_sockaddr_in, sizeof(cur_node_sockaddr_in), 0, (struct sockaddr *) &client_addr, &len);

            //add it
            add_known_node(cur_node_name, cur_node_sockaddr_in, 0);
        }
    } else if (strcmp(buffer, "ping") == 0) {
        printf("ping command received\n");
        int posnode = find_known_node(client_addr);
        printf("posnode = %d.\n", posnode);
        if (posnode == -1) {
            printf("I am going to exchange\n\n");
            exchange_info_with(client_addr);
        }
    } else {
        printf("UNKNOWN COMMAND. I don't know what \"%s\" means..... HELP PLS.\n", buffer);
    }

    printf("Closing connection with %s:%u ....\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    close(common_sock_fd);
    if (pos >= 0)
        connection_with_node[pos] = 0;
    printf("Done. Connection is closed.\n");

    free(data);
    free(buffer);
}

int ping(int i) {
    int tmp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tmp_socket == -1) {
        printf("check_available, ping: tmp_socket failed\n");
        exit(0);
    }

    int need_exchange = 0;
    connection_with_node[i] = 1; // need to do something with this.
    char *ping = "ping";
    if ((connect(tmp_socket, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address))) == 0) {
        sendto(tmp_socket, ping, strlen(ping), 0, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address));
        printf("Node with name \"%s\" is still alive, its address is %s:%u .\n", known_nodes[i].name,
               inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));
        //TODO maybe ask "do you know -(de way)- me before set needing in exchange"
//        need_exchange = 1;
    } else {
        // ONE MORE CHANCE MAFAKAAAAAAAAAAAAAAAAAAA!!1111!
        sleep(1);
        if ((connect(tmp_socket, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address))) == 0) {
            sendto(tmp_socket, ping, strlen(ping), 0, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address));
            printf("Node with name \"%s\" is still alive, its address is %s:%u .\n", known_nodes[i].name,
                   inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));
            //TODO maybe ask "do you know -(de way)- me before set needing in exchange"
//        need_exchange = 1;
        } else {
            printf("Node with name \"%s\" is not alive anymore so we delete it its address is %s:%u .\n", known_nodes[i].name,
                   inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));
            known_nodes_exist[i] = 0; // delete this node
            size_known_nodes--;
        }
    }

    close(tmp_socket);
    connection_with_node[i] = 0;
    return need_exchange;
}

//TODO inside loop add: If we don't know any node but know, from arguments, where we can connect then connect.
/** Checks all known nodes if they are still live. Node runs thread to do this job before entering main loop*/
void *check_available(void *data) {
    sleep(3);
    printf("check_available() started.\n");
    while (1) {
        printf("size of known nodes is %d\n", size_known_nodes);
        for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
            if (known_nodes_exist[i] == 1) { // 1 means there is a node
                if (connection_with_node[i] == 0) { // 0 means there is no connection right now.
                    //ping it
                    int need_exchange = ping(i);
                    if (need_exchange == 1) {
                        printf("Let's exchange\n");
                        exchange_info_with(known_nodes[i].address);
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

    memset(&our_addr, 0, sizeof(our_addr));
    our_addr.sin_family = AF_INET;
    our_addr.sin_addr.s_addr = inet_addr(IP);
    our_addr.sin_port = htons(PORT);


    if ((bind(master_sockfd, (struct sockaddr *) &our_addr, sizeof(our_addr))) == -1) {
        fprintf(stderr, "Bind() failed\n");
        exit(0);
    }
    if ((listen(master_sockfd, 10)) < 0) {
        fprintf(stderr, "Listen() failed.\n");
        exit(0);
    }

    if (argc == 4) {

        struct sockaddr_in to_connect;
        to_connect.sin_family = AF_INET;
        to_connect.sin_addr.s_addr = inet_addr(argv[2]);
        to_connect.sin_port = htons((uint16_t) atoi(argv[3]));

        printf("I am going to connect to %s:%u\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

        //TODO finish exchange.
        exchange_info_with(to_connect);
    }

    pthread_t availability_check_thread;
    pthread_create(&availability_check_thread, NULL, check_available, NULL);
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
    strcpy(myname, argv[1]);
    signal(SIGINT, sig_handler);
    setup_node(argc, argv);
    return 0;
}