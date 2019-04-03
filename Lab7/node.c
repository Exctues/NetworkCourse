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

#define PORT 2000
#define IP "10.240.21.91"

//Constants
#define MAX_IP_LENGTH 32
#define MAX_PORT_LENGTH 8
#define MAX_VISIT_LENGTH 1024
#define MAX_NEIGHBOURS 64
#define MAX_FILE_LIST_LENGTH 256
#define MAX_FILE_NAME_LENGTH 256
#define MAX_NODE_NAME_LENGTH 64
#define MAX_WORD_LENGTH 500
#define MAX_THREADS 64
#define MAX_CONNECTIONS 64
#define MAX_SYNC_IN_MOMENT 5
#define MAX_BLACKLIST_SIZE 256
//Commands
#define SYNC 1
#define REQUEST 0

//Mutex
pthread_mutex_t sync_mutex;

//Utility staff
FILE *logs;


struct Node {
    char name[MAX_NODE_NAME_LENGTH];
    struct sockaddr_in address;
    int number_of_connection_now; // we don't need it now. Reserved for future.
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

struct ip_connections {
    char ip[MAX_IP_LENGTH];
    int number_of_connections_now;
    int exist;
};

char our_files[MAX_FILE_LIST_LENGTH][MAX_FILE_NAME_LENGTH]; // here we store filenames of files we have in our "files" folder.
int our_files_exist[MAX_FILE_LIST_LENGTH]; // means the slot if free, 1 means there is a filename.
int size_our_files = 0;

struct filename_node file_list[MAX_FILE_LIST_LENGTH];
struct Node known_nodes[MAX_NEIGHBOURS];
int known_nodes_exist[MAX_NEIGHBOURS]; // 0 means the slot is free, 1 means there is a node.
int size_known_nodes = 0;

char blacklist[MAX_BLACKLIST_SIZE][MAX_IP_LENGTH];
int blacklist_exist[MAX_BLACKLIST_SIZE];

struct ip_connections ip_con[MAX_BLACKLIST_SIZE];

int master_sockfd; // it should be global because we need to close when SIGINT occurs.

// Info about myself
struct Node our_node;

/** Compares two ip's. Returns 0 if ip's are equal and -1 otherwise */
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

int find_ip_con(char *ip) {
    for (int i = 0; i < MAX_BLACKLIST_SIZE; ++i) {
        if (ip_con[i].exist == 1 && strcmp(ip_con[i].ip, ip) == 0) {

        }
    }
    return -1;
}

/** return pos where 'ip_connections struct' was inserted in ip_con array */
int add_ip_con(char *ip) {
    for (int i = 0; i < MAX_BLACKLIST_SIZE; ++i) {
        if (ip_con[i].exist == 0) {
            ip_con->exist = 1;
            ip_con->number_of_connections_now = 0;
            ip_con->exist = 1;
            strcpy(ip_con->ip, ip);
            return i;
        }
    }
    return -1;
}

int increment_ip_con(char *ip) {
    int pos = find_ip_con(ip);
    if (pos == -1) {
        int inserted_pos = add_ip_con(ip);
        if (inserted_pos >= 0) {
            ip_con[inserted_pos].number_of_connections_now++;
            return inserted_pos;
        }
    } else {
        ip_con[pos].number_of_connections_now++;
        return pos;
    }
    return -1;
}

void decrement_ip_con(char *ip) {
    int pos = find_ip_con(ip);
    ip_con[pos].number_of_connections_now--;
}

/** 0 if ip is in blacklist, -1 otherwise */
int pos_ip_in_blacklist(char *ip) {
    for (int i = 0; i < MAX_BLACKLIST_SIZE; ++i) {
        if (blacklist_exist[i] == 1 && strcmp(ip, blacklist[i]) == 0) {
            return i;
        }
    }

    return -1;
}

int blacklist_ip(char *ip) {
    for (int i = 0; i < MAX_BLACKLIST_SIZE; ++i) {
        if (blacklist_exist[i] == 0) {
            blacklist_exist[i] = 1;
            strcpy(blacklist[i], ip);
            return i;
        }
    }

    fprintf(logs, "Black list is full\n.");
    return -1;
}

/** Add name of file which is from our "files" folder */
void add_our_files(const char *filename) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (our_files_exist[i] == 0) { // if there is free slot
            strcpy(our_files[i], filename);
            our_files_exist[i] = 1;
            size_our_files++;
            fprintf(logs, "add_our_files(): filename \"%s\" successfully added..\n", filename);
            return;
        }
    }
    fprintf(logs, "add_our_file(): No space left.\n");
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

void insert_file_list(const char *filename, struct Node node) {
    fprintf(logs, "insert_file_list(): Going to add \"%s\"\n", filename);

    // Check if we already have this record.
    // IMPORTANT!! It makes system limited. We know only one node with unique filename.
    // On the other hand it is easy to maintain.
    int pos = find_pos_of_node_with_file(filename);
    if (pos == -1) {
        for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
            if (file_list[i].present == 0) {
                file_list[i].node = node;
                strcpy(file_list[i].filename, filename);
                file_list[i].present = 1;
                fprintf(logs, "insert_file_list(): \"%s\" added.\n", filename);
                return;
            }
        }
        fprintf(logs, "insert_file_list(): No space left.\n");
    } else {
        fprintf(logs, "insert_file_list(): We already have \"%s\".\n", filename);
    }
}

int find_pos_of_file_in_our_files(const char *filename) {
    for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
        if (our_files_exist[i] == 1 && strcmp(filename, our_files[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void clear_ip_con() {
    for (int i = 0; i < MAX_BLACKLIST_SIZE; ++i) {
        ip_con[i].exist = 0;
        ip_con[i].number_of_connections_now = 0;
    }
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
        fprintf(logs, "init_our_files(): Cannot open directory.\n");
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
int add_known_node(struct Node node) {
    fprintf(logs, "I am going to add node with name %s\n", node.name);
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

                fprintf(logs, "Successfully added.\n");
                return i;
            }
        }

        fprintf(logs, "add_known_node(): no space to add new node.\n");
        return -1;
    } else {
        fprintf(logs, "We already know %s\n", node.name);
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
        fprintf(logs, "no such file with name \"%s\"", filename);
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
        if (our_files_exist[i] == 1) {
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
        fprintf(logs, "parse_info_and_update(): their_node is NULL. Stop function\n");
        return -1;
    }

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
    memset(their_node, 0, sizeof(struct Node));

    strcpy(their_node->name, name);
    their_node->address.sin_family = AF_INET;
    their_node->address.sin_addr.s_addr = inet_addr(ip);
    their_node->address.sin_port = htons((uint16_t) atoi(port));

    //Process files
    if (need_files) {
        //Clear what we know before about files of their_node.
        clear_file_list_where_node(*their_node);
        fprintf(logs, "Clear filenames of node \"%s\".\n", their_node->name);
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
    return 0;
}

void sync_with(const struct sockaddr_in to_connect) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    unsigned int len;
    size_t nbytes;
    if (sockfd == -1) {
        fprintf(logs, "sync_with_known_nodes(): sockfd failed\n");
        exit(0);
    }

    if ((connect(sockfd, (struct sockaddr *) &to_connect, sizeof(to_connect))) != 0) {
        fprintf(logs, "sync_with_known_nodes(): Connection with %s:%u failed\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
        //it would be better to delete known nodes if they are dead. But for the sake of simplicity and performance(no-sync) ability let it be.
        close(sockfd);
        return;
    }

    fprintf(logs, "sync_with_known_nodes(): Connected with %s:%u.\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

    int msg = SYNC;
    nbytes = sendto(sockfd, &msg, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    if (nbytes == 0) {
        close(sockfd);
        return;
    }
    // first: send our visit.
    char our_visit[MAX_VISIT_LENGTH];
    make_visit(our_visit, size_our_files); // make our visit.
    nbytes = sendto(sockfd, &our_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    if (nbytes == 0) {
        close(sockfd);
        return;
    }

    // second: accept their's visit.
    char their_visit[MAX_VISIT_LENGTH];
    nbytes = recvfrom(sockfd, &their_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, &len);
    if (nbytes == 0) {
        close(sockfd);
        return;
    }

    // Parse it. Extract all info about files and make their_node.
    struct Node their_node;
    parse_info_and_update(their_visit, &their_node, 1);

    // third: send number of nodes we are going to send.
    int n = size_our_files;
    sendto(sockfd, &n, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));

    // fourth: send nodes
    int sended = 0;
    for (int i = 0; i < n && sended < n; ++i) {
        if (known_nodes_exist[i]) {
            char tmp_visit[MAX_VISIT_LENGTH];
            make_visit(&tmp_visit[0], 0);
            sendto(sockfd, &tmp_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
            sended++;
        }

        // Probably here should be usleep(10000);
    }

    close(sockfd);
}

void accept_sync(int sockfd, const struct sockaddr_in client_addr) {
    char ip[MAX_IP_LENGTH];
    strcpy(ip, inet_ntoa(client_addr.sin_addr));

    if (pos_ip_in_blacklist(ip) >= 0) {
        printf("%s in blacklist. Stop serve it.\n", ip);
        return;
    }

    int ip_pos = increment_ip_con(ip);
    if (ip_con[ip_pos].number_of_connections_now > MAX_SYNC_IN_MOMENT) {
        //delete node if we have and blacklist it.
        blacklist_ip(ip);
        return;
    }

    //CRITICAL ZONE
    pthread_mutex_lock(&sync_mutex);

    struct Node their_node;
    unsigned int len;
    fprintf(logs, "SYNC received.\n");

    // first we accept their's visit.
    char their_visit[MAX_VISIT_LENGTH];
    ssize_t nbytes = recvfrom(sockfd, &their_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, &len);
    if (nbytes == 0) {
        decrement_ip_con(ip);
        pthread_mutex_unlock(&sync_mutex);
        return;
    }

    // Parse it. Extract all info about files and make their_node.
    parse_info_and_update(their_visit, &their_node, 1);
    int pos_their_node = find_known_node(their_node);
    if (pos_their_node == -1) {
        pos_their_node = add_known_node(their_node);
    }
    known_nodes[pos_their_node].number_of_connection_now++;

    // send our visit.
    char our_visit[MAX_VISIT_LENGTH];
    make_visit(our_visit, size_our_files); // make our visit.
    nbytes = sendto(sockfd, &our_visit[0], MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
    if (nbytes == 0) {
        decrement_ip_con(ip);
        pthread_mutex_unlock(&sync_mutex);
        return;
    }

    //receive number of nodes we're going to get.
    int n = 0;
    nbytes = recvfrom(sockfd, &n, sizeof(int), 0, (struct sockaddr *) &client_addr, &len);
    if (nbytes == 0) {
        decrement_ip_con(ip);
        pthread_mutex_unlock(&sync_mutex);
        return;
    }

    for (int i = 0; i < n; ++i) {
        struct Node tmp_node;
        char tmp_visit[MAX_VISIT_LENGTH];
        nbytes = recvfrom(sockfd, &tmp_visit, MAX_VISIT_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, &len);
        if (nbytes == 0) {
            decrement_ip_con(ip);
            pthread_mutex_unlock(&sync_mutex);
            return;
        }
        // Parse it.
        parse_info_and_update(tmp_visit, &tmp_node, 0); // 0 - don't touch files

        // Add if we don't have such node.
        if ((find_known_node(tmp_node)) == -1) {
            add_known_node(tmp_node);
        }
    }

    decrement_ip_con(ip);
    pthread_mutex_unlock(&sync_mutex);
}

void accept_download(const int common_sock_fd, const struct sockaddr_in client_addr) {
    unsigned int len;
    ssize_t nbytes;
    fprintf(logs, "download command received.\n");

    // first: receive name of a file
    char filename[MAX_FILE_NAME_LENGTH];
    nbytes = recvfrom(common_sock_fd, &filename, MAX_FILE_NAME_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, &len);
    filename[nbytes] = '\0';
    if (nbytes == 0) {
        return;
    }

    // second: check if we have such file.
    int num_words = get_number_of_words_in(filename);

    // third: send number of words this file consists of
    nbytes = sendto(common_sock_fd, &num_words, sizeof(int), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
    if (nbytes == 0) {
        return;
    }

    // fourth: send file word-by-word
    if (num_words > 0) {
        char folder[MAX_FILE_NAME_LENGTH] = "./files/";
        FILE *file = fopen(strcat(folder, filename), "r");
        for (int i = 0; i < num_words; ++i) {
            char str[MAX_WORD_LENGTH];
            fscanf(file, "%s", str);
            nbytes = sendto(common_sock_fd, &str, MAX_WORD_LENGTH * sizeof(char), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
            if (nbytes == 0) {
                fclose(file);
                return;
            }

            usleep(10000);
        }

        fclose(file);
        printf("\"%s\" successfully uploaded.\n", filename);
        fprintf(logs, "\"%s\" successfully uploaded.\n", filename);
    } else {
        printf("We don't have file \"%s\".\n", filename);
        fprintf(logs, "We don't have file \"%s\".\n", filename);
    }

    //"we should wait until client gets the message"
    sleep(2);
}

/** Do the job with given connection (socket, client_addr) */
void *thread_work(void *data) {
    const struct thread_data *thread_data = data;
    const int common_sock_fd = thread_data->sock_fd;
    const struct sockaddr_in client_addr = thread_data->client_addr;

    unsigned int len;
    int command;

    size_t nbytes = recvfrom(common_sock_fd, &command, sizeof(int), 0, (struct sockaddr *) &client_addr, &len);
    if (nbytes == 0) {
        close(common_sock_fd);
        return 0;
    }

    if (command == SYNC) {
        accept_sync(common_sock_fd, client_addr);
    } else if (command == REQUEST) {
        accept_download(common_sock_fd, client_addr);
    } else {
        fprintf(logs, "Unknown command \"%d\" from %s\n", command, inet_ntoa(client_addr.sin_addr));
    }

    close(common_sock_fd);
    free(data);

    return 0;
}

/** Checks all known nodes if they are still live. Node runs thread to do this job before entering main loop*/
void *sync_with_known_nodes(void *data) {
    fprintf(logs, "sync_with_known_nodes() started.\n");
    while (1) {
        fprintf(logs, "-------------------------------------\n");
        fprintf(logs, "Files other nodes have:\n");
        for (int i = 0; i < MAX_FILE_LIST_LENGTH; ++i) {
            if (file_list[i].present == 1) {
                fprintf(logs, "\"%s\" from \"%s\".\n", file_list[i].filename, file_list[i].node.name);
            }
        }
        fprintf(logs, "-------------------------------------\n");
        fflush(logs);
        for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
            if (known_nodes_exist[i] == 1) {
                if ((pos_ip_in_blacklist(inet_ntoa(known_nodes[i].address.sin_addr))) == -1)
                    //update info about this node if it is exists and not in blacklist.
                    sync_with(known_nodes[i].address);
            }
        }
        sleep(3);
    }
}

void *download(void *data) {
    size_t nbytes;
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
        fprintf(logs, "*download: Connection with %s:%u failed\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));
        close(sockfd);
        return 0;
    }
    printf("*download: Connected with %s:%u successfully\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

    int msg = REQUEST;
    nbytes = sendto(sockfd, &msg, sizeof(int), 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    if (nbytes == 0) {
        close(sockfd);
        return 0;
    }

    // Send name of file we want to download
    nbytes = sendto(sockfd, filename, MAX_FILE_NAME_LENGTH, 0, (struct sockaddr *) &to_connect, sizeof(to_connect));
    if (nbytes == 0) {
        close(sockfd);
        return 0;
    }

    // Get number of words in this file.
    int num_words;
    nbytes = recvfrom(sockfd, &num_words, sizeof(int), 0, (struct sockaddr *) &to_connect, &len);
    if (nbytes == 0) {
        close(sockfd);
        return 0;
    }

    // We don't consider case when file can be empty.
    if (num_words == -1) {
        printf("*download: their node doesn't have \"%s\".\n", filename);
        fprintf(logs, "*download: their node doesn't have \"%s\".\n", filename);
        file_list[pos].present = -1; // mark that  this node doesnt have such file.
        return 0;
    }

    char folder[MAX_FILE_NAME_LENGTH] = "./files/";
    FILE *out_file = fopen(strcat(folder, filename), "w");
    for (int i = 0; i < num_words; ++i) {
        char word[MAX_WORD_LENGTH];
        nbytes = recvfrom(sockfd, &word, MAX_WORD_LENGTH * sizeof(char), 0, (struct sockaddr *) &to_connect, &len);
        if (nbytes == 0) {
            fclose(out_file);
            close(sockfd);
            return 0;
        }
        word[nbytes] = '\0';
        fprintf(out_file, "%s ", word);
    }

    add_our_files(filename);

    printf("\"%s\" successfully downloaded.\n ", filename);
    fprintf(logs, "\"%s\" successfully downloaded.\n ", filename);

    close(sockfd);
    fclose(out_file);

    return 0;
}

/** Thread constantly waits for a command */
void *interact_with_user(void *data) {
    //TODO add "sync" command so that user can make connection manually during runtime.
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
        sleep(5);
    }
}

void setup_node(int argc, char **argv) {
    master_sockfd = socket(AF_INET, SOCK_STREAM, 0); // master socket. Used as entrance point only.

    if (master_sockfd == -1) {
        fprintf(stderr, "Socket() failed\n");
        fprintf(logs, "Socket() failed\n");
        exit(0);
    }

    // This option allow us to use the same port just right after program is closed\killed. So we don't need to wait.
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
        fprintf(logs, "Bind() failed\n");
        exit(0);
    }
    if ((listen(master_sockfd, MAX_CONNECTIONS)) < 0) {
        fprintf(logs, "Listen() failed.\n");
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

        fprintf(logs, "I am going to sync with entrance point %s:%u\n", inet_ntoa(to_connect.sin_addr), ntohs(to_connect.sin_port));

        sync_with(to_connect);
    }

    fd_set readfds;
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(master_sockfd, &readfds);

        fprintf(logs, "I am waiting on select\n");
        select(master_sockfd + 1, &readfds, NULL, NULL, NULL); //Blocks until any data arrives to any of the FD's in readfds.

        if (FD_ISSET(master_sockfd, &readfds)) {
            struct sockaddr_in client_addr;
            memset(&client_addr, 0, sizeof(client_addr));

            unsigned int addr_len = sizeof(struct sockaddr);
            int common_sock_fd = accept(master_sockfd, (struct sockaddr *) &client_addr, &addr_len);
            if (common_sock_fd < 0) {
                fprintf(logs, "accept error : errno = %d\n", errno);
                exit(0);
            }

            fprintf(logs, "Connection accepted from node: %s:%u\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

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

void init_mutexes() {
    if (pthread_mutex_init(&sync_mutex, NULL) != 0) {
        printf("\n mutex init failed\n");
        fprintf(logs, "\n mutex init failed\n");
        exit(0);
    }
}

/** ctrl+c is the only way to exit correctly */
void sig_handler(int sig) {
    if (sig == SIGINT) {
        printf("\nReceived SIGINT. Exiting...\n");
    }
    close(master_sockfd);
    fclose(logs);
    pthread_mutex_destroy(&sync_mutex);
    exit(0);
}

/** IMPORTANT Put "FILES" folder near this file before run!!!!!1111!
 * Compile with your ip (use ifconfig to check) and port. You can change it in constants block.
 * Always run with name as argument. Then ip and port if you want to connect somewhere first.
 * All files which you want to share should be in folder "files". This folder and executable should be in the same place.
 * To download file during runtime type in console "download filename.txt"
 * Example of launching:
 * 1) ./node.out lol. -- node with name lol which just wait.
 * 2) ./node.out kek 192.168.0.1 2000 -- node with name "kek" which immediately connects to 192.168.0.1:2000.
 *
 * There will create file with name "logs.txt" where all logs will be saved.
 * */
int main(int argc, char **argv) {
    if (argc == 1 || argc == 3 || argc > 4) {
        printf("Number of arguments must be either 1 or 3. Program exiting ... \n");
        exit(0);
    }
    signal(SIGINT, sig_handler);
    logs = fopen("logs.txt", "w");

    //clearing
    memset(known_nodes_exist, 0, sizeof(int) * MAX_NEIGHBOURS);
    memset(our_files_exist, 0, sizeof(int) * MAX_FILE_LIST_LENGTH);
    memset(blacklist_exist, 0, sizeof(int) * MAX_BLACKLIST_SIZE);
    clear_ip_con();
    clear_file_list();

    init_mutexes();

    //init files && start node.
    init_our_files();

    //open log file.
    setup_node(argc, argv);

    return 0;
}