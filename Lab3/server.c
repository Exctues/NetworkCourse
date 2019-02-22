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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*Server process is running on this port no. Client has to send data to this port no*/
const unsigned int SERVER_PORT = 2001;

test_struct_t test_struct;
result_struct_t res_struct;

const int BUFFER_SIZE = 1024;

// get sockaddr of IPv4
void *get_in_addr(struct sockaddr *sa)
{
    return &(((struct sockaddr_in*)sa)->sin_addr);
}

void setup_udp_server_communication() {
    int udp_fd = 0;
    
    socklen_t addr_len = 0;

    struct sockaddr_storage their_addr;
    
    // this is for creating socket and bind it
    struct addrinfo *servinfo;
	struct addrinfo hints;

    memset(&hints, 0, sizeof hints);
    // Po idei mojno eto ne dealt esli v socket() yavno ukazivat, ny da lando, na vsyakiy sluchai
	hints.ai_family = AF_INET; 
    hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
    
    int rv; // returned values
	if ((rv = getaddrinfo(NULL, SERVER_PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

    struct addrinfo *p; 
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((udp_fd = socket(AF_INET, SOCK_DGRAM, SOCK_DGRAM)) == -1) {
			perror("Server: socket error\n");
			continue;
        }

		if (bind(udp_fd, p->ai_addr, p->ai_addrlen) == -1) {
			close(udp_fd);
			perror("Server: bind error. Socket is closed.\n");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "Server: failed to bind socket\n");
		exit(1);
	}

    // Delete our Linked List.
    freeaddrinfo(servinfo);

    printf("Server enter endless loop.\n");
    while (1) {
        char data_buffer[BUFFER_SIZE];
        memset(data_buffer, 0, sizeof(data_buffer));

        test_struct_t *client_data = (test_struct_t *) data_buffer;

        addr_len = sizeof their_addr;
        int nbytes = 0;
        if ((nbytes = recvfrom(udp_fd, data_buffer, BUFFER_SIZE-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }
        char s[INET_ADDRSTRLEN]; // for client address
        printf("Server: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr),s, sizeof s));
		
        printf("Server: packet is %d bytes long\n", nbytes);
        data_buffer[nbytes] = '\0';
        printf("Server: packet contains \"%s\"\n", data_buffer); 

        close(udp_fd);
        break;      
    }
}

int main(int argc, char **argv) {
    setup_udp_server_communication();
    return 0;
}
