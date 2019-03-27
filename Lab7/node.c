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

//Constants
#define MAX_IP_LENGTH 32
#define MAX_PORT_LENGTH 8
#define MAX_VISIT_LENGTH 1024
#define MAX_NEIGHBOURS 64
#define MAX_FILE_LIST_LENGTH 256
#define MAX_FILE_NAME_LENGTH 256
#define MAX_NODE_NAME_LENGTH 64
#define MAX_WORD_LENGTH 64
#define MAX_THREADS 64
#define MAX_CONNECTIONS 16

//Commands
#define SYNC 1
#define REQUEST 0

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
int known_nodes_exist[MAX_NEIGHBOURS];    // 0 means the slot is free, 1 means there is a node.
int size_known_nodes = 0;

int master_sockfd; //it should be global because we need to close when SIGINT occurs.

// Info about myself
struct Node our_node;

/** Compares two ip's. Returns 0 if ip's are equal and -1 otherwise*/
int addrcmp(const struct sockaddr_in a, const struct sockaddr_in b) {
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
void add_our_files(const char *filename) {
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

int find_pos_of_node_with_file(const char *filename) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (file_list[i].present == 1) {
            if (strcmp(file_list[i].filename, filename) == 0) {
                return i;
            }
        }
    }

    return -1;
}

void insert_file_list(const char *filename, const struct Node node) {
    fprintf(stderr, "insert_file_list(): Going to add \"%s\"\n", filename);

    // Check if we already have this record.
    // IMPORTANT!! It makes system limited. We know only one node with unique filename.
    // but it is easy to maintain.
    int pos = find_pos_of_node_with_file(filename);
    if (pos == -1) {
        for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
            if (file_list[i].present == 0) {
                file_list[i].node = node;
                strcpy(file_list[i].filename, filename);
                file_list[i].present = 1;
                fprintf(stderr, "insert_file_list(): \"%s\" added.\n", filename);
                return;
            }
        }
        printf("insert_file_list(): No space left.\n");
    } else {
        fprintf(stderr, "insert_file_list(): We already have \"%s\".\n", filename);
    }
}

int find_pos_of_file_in_our_files(const char *filename) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (our_files_exists[i] == 1 && strcmp(filename, our_files[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void clear_file_list() {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i)
        file_list[i].present = 0;
}

void clear_file_list_where_node(const struct Node node) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i)
        if (file_list[i].present == 1 && addrcmp(node.address, file_list[i].node.address) == 0)
            file_list[i].present = 0;
}

/** Add all names of files which are from our "files" folder */
void init_our_files() {
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

/** Search by node's address. Returns pos of that node and -1 if it doesnt exist*/
int find_known_node(const struct Node node) {
    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (known_nodes_exist[i] == 1 && addrcmp(node.address, known_nodes[i].address)) {
            return i;
        }
    }

    return -1;
}

/** Finds new free slot and adds new assembled node in that slot. Returns pos of new added Node*/
int add_known_node(const struct Node node) {
    fprintf(stderr, "I am going to add node with name %s\n", node.name);
    if (addrcmp(node.address, our_node.address) == 0) {
        //Prevent to add myself
        return -1;
    }

    int pos = find_known_node(node);
    if (pos == -1) {
        for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
            if (known_nodes_exist[i] == 0) {
                known_nodes[i] = node;
                known_nodes_exist[i] = 1;

                size_known_nodes++;

                fprintf(stderr, "Successfully added.\n");
                return i;
            }
        }

        fprintf(stderr, "add_known_node(): no space to add new node.\n");
        return -1;
    } else {
        fprintf(stderr, "We already know %s\n", node.name);
        return pos;
    }
}

/** -1 means file doesn't exist. */
int get_number_of_words_in(const char *filename) {
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

/** We accept size_of_files as argument because number of
 *  files we have can change during for-loop and we will
 *  have wrong number of files in our visit.
 *  It protects only in case # of nodes increased.*/
void make_visit(char *visit, int size_of_files) {
    visit[0] = '\0'; // it will make strcat() work correct.

    // add name
    strcat(visit, our_node.name);
    strcat(visit, ":");

    // add ip
    strcat(visit, IP);
    strcat(visit, ":");

    char port[MAX_PORT_LENGTH];
    sprintf(port, "%d", PORT);

    // add port
    strcat(visit, port);
    strcat(visit, ":");


    int files_written = 0;

    for (int i = 0; i < MAX_FILE_LIST_LENGTH && files_written < size_of_files; ++i) {
        if (our_files_exists[i] == 1) {
            strcat(visit, our_files[i]);
            files_written++;

            if (files_written < size_our_files)
                strcat(visit, ",");
        }
    }

}

/** Obsolete: if their_node ==  NULL then function will not construct it from visit's data.*/
/** if their_node ==  NULL then function will return -1 .*/
int parse_info_and_update(const char *visit, struct Node *their_node, int need_files) {
    if (their_node == NULL) {
        fprintf(stderr, "parse_info_and_update(): their_node is NULL. Stop function\n");
        return -1;
    }
//    fprintf(stderr, "parse_info_and_update(): visit is \"%s\"\n", visit);

    char name[MAX_NODE_NAME_LENGTH];
    char ip[MAX_IP_LENGTH];
    char port[MAX_PORT_LENGTH];

    //process name
    int cur_pos = 0;

    for (int i = 0; visit[cur_pos] != ':'; i++) {
        name[i] = visit[cur_pos];
        cur_pos++;
    }
    name[cur_pos] = '\0';
    cur_pos++;

    //process ip
    for (int i = 0; visit[cur_pos] != ':'; i++) {
        ip[i] = visit[cur_pos];
        cur_pos++;
    }
    ip[cur_pos] = '\0';
    cur_pos++;

    //process port
    for (int i = 0; visit[cur_pos] != ':'; i++) {
        port[i] = visit[cur_pos];
        cur_pos++;
    }
    port[cur_pos] = '\0';
    cur_pos++;

    //set up their_node
//    struct Node tmp_node; // TODO think about it.
    memset(their_node, 0, sizeof(struct Node));

    strcpy(their_node->name, name);
    their_node->address.sin_family = AF_INET;
    their_node->address.sin_addr.s_addr = inet_addr(ip);
    their_node->address.sin_port = htons((uint16_t) atoi(port));

    //Process files
    if (need_files) {
        //Clear what we know before about files of their_node.
        clear_file_list_where_node(*their_node);

        //Make new knowledge about files of their_node
        while (visit[cur_pos] != '\0') {
            char filename[MAX_FILE_NAME_LENGTH];
            for (int i = 0; visit[cur_pos] != ',' && visit[cur_pos] != '\0'; i++) {
                filename[i] = visit[cur_pos];
                cur_pos++; // do not increment it anywhere else!!!
            }
            filename[cur_pos] = '\0';
            insert_file_list(filename, *their_node);

            if (visit[cur_pos] != '\0') // if end of filenames.
                cur_pos++;
        }
    }
    fprintf(stderr, "parse_info_and_update(): DONE\n");
    return 0;
}

void sync_with(const struct sockaddr_in to_connect) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    unsigned int len;
    if (sockfd == -1) {
        printf("sync_with_known_nodes: sockfd failed\n");
        exit(0);
    }

    if ((connect(sockfd, (struct sockaddr *) &to_connect, sizeof(to_connect))) != 0) {
        printf("sync_with_known_nodes: Connection with %s:%u failed\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
        //TODO it would be better to delete known nodes if they are dead. But for the sake of simplicity and no-sync ability let it be.
        close(sockfd);
        return;
    }
//    fprintf(stderr, "Connected with %s:%u successfully\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

    int msg = SYNC;
    sendto(sockfd, &msg, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    // first: send our visit.
    char our_visit[MAX_VISIT_LENGTH];
    make_visit(our_visit, size_our_files); // make our visit.
    sendto(sockfd, &our_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    // second: accept their's visit.
    char their_visit[MAX_VISIT_LENGTH];
    recvfrom(sockfd, &their_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, &len);

    // Parse it. Extract all info about files and make their_node.
    struct Node their_node;
    parse_info_and_update(their_visit, &their_node, 1);

    // third: take n of nodes to accept.
    int n = 0;
    recvfrom(sockfd, &n, sizeof(int), 0, (struct sockaddr *) &to_connect, &len);
    for (int i = 0; i < n; ++i) {
        struct Node tmp_node;
        char tmp_visit[MAX_VISIT_LENGTH];
        recvfrom(sockfd, &tmp_visit, MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, &len);

        // Parse it.
        parse_info_and_update(tmp_visit, &tmp_node, 0); // 0 - don't touch files

        // Add if we don't have such node.
        if ((find_known_node(tmp_node)) == -1) {
            add_known_node(tmp_node);
        }
    }

    close(sockfd);

//    printf("Socket to communicate with \"%s\" is closed.\n", their_node.name);
}

void accept_sync(const int common_sock_fd, const struct sockaddr_in client_addr) {
    struct Node their_node;
    unsigned int len;
    fprintf(stderr, "SYNC received.\n");

    // first we accept their's visit.
    char their_visit[MAX_VISIT_LENGTH];
    recvfrom(common_sock_fd, &their_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, &len);

    // Parse it. Extract all info about files and make their_node.
    parse_info_and_update(their_visit, &their_node, 1);
    if ((find_known_node(their_node)) == -1) {
        add_known_node(their_node);
    }

    // send our visit.
    char our_visit[MAX_VISIT_LENGTH];
    make_visit(our_visit, size_our_files); // make our visit.
    sendto(common_sock_fd, &our_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));

    // send number of nodes we are going to send
    int n = size_our_files;
    sendto(common_sock_fd, &n, sizeof(int), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));

    //send nodes
    int sended = 0;
    for (int i = 0; i < n && sended < n; ++i) {
        if (known_nodes_exist[i]) {
            char tmp_visit[MAX_VISIT_LENGTH];
            make_visit(&tmp_visit[0], 0);
            sendto(common_sock_fd, &tmp_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
            sended++;
        }
    }

}

void accept_download(const int common_sock_fd, const struct sockaddr_in client_addr) {
    unsigned int len;
    ssize_t nbytes;
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
}

/** Do the job with given connection (socket, client_addr) */
void *thread_work(void *data) {
    const struct thread_data *thread_data = data;
    const int common_sock_fd = thread_data->sock_fd;
    const struct sockaddr_in client_addr = thread_data->client_addr;

    unsigned int len; // we need it just to use in recvfrom.
    int command;

    recvfrom(common_sock_fd, &command, sizeof(int), 0, (struct sockaddr *) &client_addr, &len);

    if (command == SYNC) {
        accept_sync(common_sock_fd, client_addr);
    } else if (command == REQUEST) {
        accept_download(common_sock_fd, client_addr);
    } else {
        printf("Unknown command \"%d\" from %s\n", command, inet_ntoa(client_addr.sin_addr));
    }

    close(common_sock_fd);
    free(data);

    return 0;
}

/** Checks all known nodes if they are still live. Node runs thread to do this job before entering main loop*/
void *sync_with_known_nodes(void *data) {
    fprintf(stderr, "sync_with_known_nodes() started.\n");
    while (1) {
        fprintf(stderr, "Files other nodes have:\n");
        for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
            if (file_list[i].present == 1) {
                fprintf(stderr, "\"%s\" from \"%s\".\n", file_list[i].filename, file_list[i].node.name);
            }
        }
        fprintf(stderr, "\n\n");

        printf("size of known nodes is %d\n", size_known_nodes);
        for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
            if (known_nodes_exist[i] == 1) { // 1 means there is a node
                //update info about this node.
                sync_with(known_nodes[i].address);
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

    int msg = REQUEST;
    sendto(sockfd, &msg, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

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
    //TODO add "sync" command so that user can make connection manually.
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

    pthread_create(&availability_check_thread, NULL, sync_with_known_nodes, NULL);
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

    fd_set readfds;
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

//            fprintf(stderr, "Connection accepted from node: %s:%u\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            struct thread_data *thread_data = malloc(sizeof(thread_data));
            memset(thread_data, 0, sizeof(struct thread_data)); // debug
            thread_data->sock_fd = common_sock_fd;
            thread_data->client_addr = client_addr;

            pthread_create(&pthreads[next_thread], NULL, thread_work, (void *) thread_data);
            next_thread++;
            if (next_thread >= MAX_THREADS) {
                // In real life it is better to create thread pool and save threads there
                // But this is C.
                next_thread = 0;
            }
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
 * Compile with your ip (use ifconfig to check) and port. You can change it in constants block.
 * Always run with name as argument. Then ip and port if you want to connect somewhere first.
 * All files which you want to share should be in folder "files". This folder and executable should be in the same place.
 * To download file during runtime type in console "download filename.txt"
 * Example of launching:
 * 1) ./node.out lol. -- node with name lol which just wait.
 * 2) ./node.out kek 192.168.0.1 2000 -- node with name "kek" which immediately connects to 192.168.0.1:2000.
 * */
int main(int argc, char **argv) {
    if (argc == 1 || argc == 3 || argc > 4) {
        printf("Number of arguments must be either 1 or 3. Program exiting ... \n");
        exit(0);
    }
    signal(SIGINT, sig_handler);

    //clearing
    memset(known_nodes_exist, 0, sizeof(int) * MAX_NEIGHBOURS);
    memset(our_files_exists, 0, sizeof(int) * MAX_FILE_LIST_LENGTH);
    clear_file_list();

    //init files && start node.
    init_our_files();
    setup_node(argc, argv);

    return 0;
}