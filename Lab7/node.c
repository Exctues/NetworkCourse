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

#define PORT 2002
#define IP "192.168.31.105"

#define MAX_NEIGHBOURS 32
#define MAX_FILE_LIST_LENGTH 128
#define MAX_NODE_NAME_LENGTH 64
#define MAX_FILE_NAME_LENGTH 256
#define MAX_WORD_LENGTH 64
#define MAX_THREADS 64
#define MAX_CONNECTIONS 16
// ip 192.167.31.105

struct Node {
    char name[MAX_NODE_NAME_LENGTH];
    struct sockaddr_in address;
};
struct thread_data {
    int sock_fd;
    struct sockaddr_in client_addr;
};
struct filename_node {
    char filename[MAX_FILE_NAME_LENGTH];
    struct Node node;
    int present; //0 means there is no such node, 1 means you can use this node to download file.
};

char our_files[MAX_FILE_LIST_LENGTH][MAX_FILE_NAME_LENGTH]; // here we store filenames of files we have in our "files" folder.
int our_files_exists[MAX_FILE_LIST_LENGTH]; // means the slot if free, 1 means there is a filename.
int size_our_files = 0;

struct filename_node file_list[MAX_FILE_LIST_LENGTH];
struct Node known_nodes[MAX_NEIGHBOURS];
int connection_with_node[MAX_NEIGHBOURS]; // 0 means we don't have open connection with this node. 1 means we have.
int known_nodes_exist[MAX_NEIGHBOURS];    // 0 means the slot is free, 1 means there is a node.
int size_known_nodes = 0;

int master_sockfd;
fd_set readfds;

// Info about myself
struct Node our_node;

/** Compares two ip's. Returns 0 if ip's are equal and -1 otherwise*/
int addrcmp(struct sockaddr_in a, struct sockaddr_in b) {
    // TODO not urgent: change passing whole struct to passing just a pointer.
    char astr[32];
    char bstr[32];
    strcpy(astr, inet_ntoa(a.sin_addr));
    strcpy(bstr, inet_ntoa(b.sin_addr));
    if (strcmp(astr, bstr) == 0 && a.sin_port == b.sin_port) {
        return 0;
    }
    return -1;
}

/** Add name of file which is from our "files" folder */
void add_our_files(char *filename) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (our_files_exists[i] == 0) { // if there is free slot
            strcpy(our_files[i], filename);
            our_files_exists[i] = 1;
            size_our_files++;
            printf("add_our_files(): filename \"%s\" successfully added..\n", filename);
            return;
        }
    }
    printf("add_our_file(): No space left.\n");
}

void insert_file_list(char *filename, struct Node node) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (file_list[i].present == 0) {
            file_list[i].node = node;
            printf("insert_file_list(): %s added.\n", filename);
            strcpy(file_list[i].filename, filename);
            file_list[i].present = 1;
            return;
        }
    }
    printf("insert_file_list(): No space left.\n");

}

int find_pos_of_file_in_our_files(char *filename) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (our_files_exists[i] == 1 && strcmp(filename, our_files[i]) == 0) {
            return i;
        }
    }
    return -1;
}

int find_pos_of_node_with_file(char *filename) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (file_list[i].present == 1 && strcmp(file_list[i].filename, filename) == 0) {
            return i;
        }
    }

    return -1;
}

void clear_file_list() {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i)
        file_list[i].present = 0;
}

void clear_file_list_where_node(struct Node node) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i)
        if (file_list[i].present == 1 && addrcmp(node.address, file_list[i].node.address) == 0)
            file_list[i].present = 0;
}

/** Add all names of files which are from our "files" folder */
void init_our_files() {
    //TODO not urgent: inside loop miss first two variants because they are "." and "..".
    clear_file_list();
    DIR *dir;
    struct dirent *ent;
    char *folder = "./files";
    if ((dir = opendir(folder)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(".", ent->d_name) != 0 && strcmp("..", ent->d_name) != 0) {
                add_our_files(ent->d_name);
            }
        }
        closedir(dir);
    } else {
        printf("init_our_files(): Cannot open directory.\n");
    }

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

    printf("add_known_node(): no space to add new node.\n");
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

/** -1 means file doesn't exist. */
int get_number_of_words_in(char *filename) {
    char folder[MAX_FILE_NAME_LENGTH] = "./files/";
    char full_relative_path[MAX_FILE_NAME_LENGTH];
    strcpy(full_relative_path, strcat(folder, filename));

    FILE *file = fopen(full_relative_path, "r");
    if (file != NULL) {
        int n = 0;
        char str[MAX_WORD_LENGTH];
        while (fscanf(file, "%s", str) != EOF) {
            n++;
        }
        fclose(file);
        return n;
    } else {
        printf("no such file with name \"%s\"", filename);
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
    char *msg = "sync";
    sendto(sockfd, msg, MAX_WORD_LENGTH, 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    // Nodes sync

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
    // filenames sync

    //send
    n = size_our_files;
    sendto(sockfd, &n, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    for (int i = 0, sended = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (our_files_exists[i] == 1) {
            sendto(sockfd, &our_files[i], MAX_FILE_NAME_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
            sended++;
        }
    }

    // delete what we know before
    clear_file_list_where_node(their_node);

    printf("hey3\n");
    //receive
    int size_their_filenames;
    recvfrom(sockfd, &size_their_filenames, sizeof(int), 0, (struct sockaddr *) &to_connect, &len);
    printf("hey3.5 size = %d\n", size_their_filenames);
    for (int i = 0; i < size_their_filenames; ++i) {
        printf("hey4\n");
        char cur_filename[MAX_FILE_NAME_LENGTH];
        recvfrom(sockfd, &cur_filename, MAX_FILE_NAME_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, &len);
        insert_file_list(cur_filename, their_node);
    }

    close(sockfd);
    if (pos >= 0) // we should mark that connection is closed
        connection_with_node[pos] = 0;

    printf("Socket to communicate with \"%s\" is closed.\n", their_node.name);
}


/** Do the job with given connection (socket, client_addr) */
void *thread_work(void *data) {
    struct thread_data *thread_data = data;
    int common_sock_fd = thread_data->sock_fd;
    struct sockaddr_in client_addr = thread_data->client_addr;

    ssize_t nbytes;
    unsigned int len;
    char command[MAX_WORD_LENGTH];

    nbytes = recvfrom(common_sock_fd, command, MAX_WORD_LENGTH, 0, (struct sockaddr *) &client_addr, &len);
    command[nbytes] = '\0';

    int pos = -1;
    struct Node their_node;
    if (strcmp(command, "sync") == 0) {
        printf("sync command received.\n");
        // Nodes sync

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

        // 5) we receive their_size_nodes = number of nodes this node knows.
        int their_size_nodes;
        recvfrom(common_sock_fd, &their_size_nodes, sizeof(int), 0, (struct sockaddr *) &client_addr, &len);

        // 6) we receive this their_size_nodes nodes.
        for (int i = 0; i < their_size_nodes; ++i) {
            struct Node tmp_node;
            recvfrom(common_sock_fd, &tmp_node, sizeof(struct Node), 0, (struct sockaddr *) &client_addr, &len);
            if (find_known_node(tmp_node) == -1) {
                add_known_node(tmp_node, 0);
            }
        }

        // filenames sync

        // delete what we know before
        clear_file_list_where_node(their_node);

        //receive
        int size_their_filenames;
        recvfrom(common_sock_fd, &size_their_filenames, sizeof(int), 0, (struct sockaddr *) &client_addr, &len);

        for (int i = 0; i < size_their_filenames; ++i) {
            char cur_filename[MAX_FILE_NAME_LENGTH];
            recvfrom(common_sock_fd, &cur_filename, MAX_FILE_NAME_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, &len);
            insert_file_list(cur_filename, their_node);
        }

        //send
        n = size_our_files;
        sendto(common_sock_fd, &n, sizeof(int), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
        for (int i = 0, sended = 0; i < MAX_FILE_LIST_LENGTH && sended < n; ++i) {
            if (our_files_exists[i] == 1) {
                sendto(common_sock_fd, &our_files[i], MAX_FILE_NAME_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
                sended++;
            }
        }

    } else if (strcmp(command, "ping") == 0) {
        printf("ping command received\n");

    } else if (strcmp(command, "download") == 0) {
        printf("download command received");
        // 1) Wait for the name of file
        char filename[MAX_FILE_NAME_LENGTH];
        nbytes = recvfrom(common_sock_fd, &filename, MAX_FILE_NAME_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, &len);
        filename[nbytes] = '\0';

        // 2) Check if we have such file
        int num_words = get_number_of_words_in(filename);
        printf("num_words REALLY = %d\n", num_words);
        // 3) If file exists then send number of words this file has and -1 otherwise
        sendto(common_sock_fd, &num_words, sizeof(int), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));

        // 4) send file word-by-word
        if (num_words > 0) {
            char folder[MAX_FILE_NAME_LENGTH] = "./files/";
            FILE *file = fopen(strcat(folder, filename), "r");
            for (int i = 0; i < num_words; ++i) {
                char str[MAX_WORD_LENGTH];
                fscanf(file, "%s", str);
                sendto(common_sock_fd, &str, MAX_WORD_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
            }
        }

        printf("\"%s\" successfully uploaded.\n ", filename);
    } else {
        printf("Unknown command \"%s\" from %s\n", command, inet_ntoa(client_addr.sin_addr));
    }

    close(common_sock_fd);

    if (pos >= 0) { // we should mark that connection is closed
        connection_with_node[pos] = 0;
    }

    free(data);
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
        printf("Second chance for node wit name\"%s\".\n", known_nodes[i].name);
        sleep(1);
        if ((connect(tmp_socket, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address))) == 0) {
            sendto(tmp_socket, ping, strlen(ping), 0, (struct sockaddr *) &known_nodes[i].address, sizeof(known_nodes[i].address));
            printf("Node with name \"%s\" is still alive, its address is %s:%u.\n", known_nodes[i].name,
                   inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));
        } else {
            printf("Node with name \"%s\" is not alive anymore so we delete it. Its address is %s:%u.\n", known_nodes[i].name,
                   inet_ntoa(known_nodes[i].address.sin_addr), ntohs(known_nodes[i].address.sin_port));

            clear_file_list_where_node(known_nodes[i]); // clear our info about files this node has.
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
        sleep(7);
    }
}

void *download(void *data) {
    char *filename = (char *) data;
    printf("I want to download \"%s\".\n", filename);

    int pos = find_pos_of_file_in_our_files(filename);
    if (pos >= 0) {
        printf("I have this file already. It is in \"files\" folder.\n");
        return 0;
    }

    pos = find_pos_of_node_with_file(filename);
    if (pos == -1) {
        printf("*download: We don't know any node which has \"%s\" file\n", filename);
        return 0;
    }

    struct sockaddr_in to_connect;
    to_connect = file_list[pos].node.address;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    unsigned int len;
    if (sockfd == -1) {
        printf("*download(): sockfd failed\n");
        return 0;
    }

    if ((connect(sockfd, (struct sockaddr *) &to_connect, sizeof(to_connect))) != 0) {
        printf("*download: Connection with %s:%u failed\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
        close(sockfd);
        return 0;
    }
    printf("*download: Connected with %s:%u successfully\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

    char *msg = "download";
    sendto(sockfd, msg, MAX_WORD_LENGTH, 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    // Send name of file we want to download
    sendto(sockfd, filename, MAX_FILE_NAME_LENGTH, 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    // Get number of words in this file.
    int num_words;
    recvfrom(sockfd, &num_words, sizeof(int), 0, (struct sockaddr *) &to_connect, &len);
    if (num_words == -1) {
        printf("*download: their node doesn't have \"%s\".\n", filename);
        file_list[pos].present = -1; // mark that  this node doesnt have such file.
        return 0;
    }
    char folder[MAX_FILE_NAME_LENGTH] = "./files/";
    FILE *out_file = fopen(strcat(folder, filename), "w");
    for (int i = 0; i < num_words; ++i) {
        char word[MAX_WORD_LENGTH];
        ssize_t nbytes = recvfrom(sockfd, &word, MAX_WORD_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, &len);
        word[nbytes] = '\0';
        fprintf(out_file, "%s ", word);
    }
    add_our_files(filename);
    printf("\"%s\" successfully downloaded.\n ", filename);
    close(sockfd);
    fclose(out_file);
}

/** Thread constantly waits for a command */
void *interact_with_user(void *data) {
    //TODO add "sync" command.
    pthread_t tmp_thread;
    while (1) {
        printf("I am waiting for the command: \n");
        char command[MAX_WORD_LENGTH];
        scanf("%s", command);
        if (strcmp(command, "download") == 0) {
            char filename[MAX_FILE_NAME_LENGTH];
            scanf("%s", filename);
            printf("\n");

            // create new thread to work with it
            pthread_create(&tmp_thread, NULL, download, filename);

        } else {
            printf("Unknown command.\n");
        }
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
    if ((listen(master_sockfd, MAX_CONNECTIONS)) < 0) {
        fprintf(stderr, "Listen() failed.\n");
        exit(0);
    }

    pthread_t availability_check_thread;
    pthread_t interactable_thread;

    pthread_create(&availability_check_thread, NULL, ping_know_nodes, NULL);
    pthread_create(&interactable_thread, NULL, interact_with_user, NULL);

    pthread_t pthreads[MAX_THREADS];
    unsigned int next_thread = 0;

    if (argc == 4) { // need to connect to some entrance point first
        struct sockaddr_in to_connect;
        to_connect.sin_family = AF_INET;
        to_connect.sin_addr.s_addr = inet_addr(argv[2]);
        to_connect.sin_port = htons((uint16_t) atoi(argv[3]));

        printf("I am going to sync with entrance point %s:%u\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

        sync_with(to_connect);
    }

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
 * Always run with name as argument. Then ip and port if you want to connect somewhere first.
 * All files which you want to share should be in folder "files". This folder and executable should be in the same place.
 * To download file during runtime type in console "download filename.txt"
 * Example of lauching:
 * 1) ./node.out lol. -- node with name lol
 * 2) ./node.out kek 192.168.0.1 2000 -- node with name "kek" which immedi connects to 192.168.0.01:2000 node.
 * */
int main(int argc, char **argv) {
    if (argc == 1 || argc == 3 || argc > 4) {
        printf("Number of arguments must be either 1 or 3. Program exiting ... \n");
        exit(0);
    }

    //clearing
    memset(known_nodes_exist, 0, sizeof(int) * MAX_NEIGHBOURS);
    memset(our_files_exists, 0, sizeof(int) * MAX_FILE_LIST_LENGTH);
    memset(connection_with_node, 0, sizeof(int) * MAX_NEIGHBOURS);
    clear_file_list();

    //init files && start node.
    init_our_files();
    signal(SIGINT, sig_handler);
    setup_node(argc, argv);

    return 0;
}