#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

/* Print colors */
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define FALSE 0
#define TRUE 1

/* Reads message from socket and writes it to the specified buffer */
static inline int receive(const int sd, char* ret_buffer, const int size) {
    int tot_size = 0;
    while (tot_size < size) {
        const int curr_size = recv(sd, &ret_buffer[tot_size], size - tot_size, 0);
        if (curr_size <= 0) {
            // An error occurred
            return -1;
        }
        tot_size += curr_size;
    }
    return 0;
}

int main(int argc, char* args[]) {
    if (argc < 2) {
        printf("Usage: <port>\n");
        exit(1);
    }
    const int port = strtol(args[1], NULL, 10);
    struct sockaddr_in server_address = {
        sin_family: AF_INET,
        sin_port : htons(port),
        sin_addr : {
            s_addr: INADDR_ANY
        },
    };

    /* Create a new socket */
    const int socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor < 0) {
        perror("socket");
        exit(1);
    }

    /* Set socket option REUSE_ADDRESS */
    int optvalue = TRUE;
    if (setsockopt(socketDescriptor, SOL_SOCKET, SO_REUSEADDR, (const char*)&optvalue, sizeof(optvalue)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
    }
    #ifdef SO_REUSEPORT
        if (setsockopt(socketDescriptor, SOL_SOCKET, SO_REUSEPORT, (const char*)&optvalue, sizeof(optvalue)) < 0) {
            perror("setsockopt(SO_REUSEPORT");
        }
    #endif

    /* Bind the socket to the specified port number */
    if (bind(socketDescriptor, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        perror("bind");
        exit(1);
    }
    /* Set the maximum queue length for clients requesting connection to 5 */
    if (listen(socketDescriptor, 5) < 0) {
        perror("listen");
        exit(1);
    }

    while (TRUE)
    {
        struct sockaddr_in address;
        int addressLength = sizeof(address);
        printf(ANSI_COLOR_BLUE "Monitor: Waiting for connections...\n");
        const int currentSocketDescriptor = accept(socketDescriptor, (struct sockaddr*)&address, (socklen_t*)&addressLength);
        if (currentSocketDescriptor < 0) {
            perror("accept");
            exit(1);
        }
        /* When execution reaches this point a client established the connection.
           The returned socket (currSd) is used to communicate with the client */
        printf("Monitor: Connection established from %s\n", inet_ntoa(address.sin_addr));
        /* As first message receive the number of consumers */
        int consumers_number = 0;
        if (receive(currentSocketDescriptor, (char*)&consumers_number, sizeof(int)) < 0) {
            perror("receive");
            exit(1);
        }
        printf("Monitor: Received consumers number -> %d\n" ANSI_COLOR_RESET, consumers_number);

        int message[consumers_number + 2];
        while (TRUE) {
            if (receive(currentSocketDescriptor, (char*)&message, sizeof(message)) < 0) {
                printf(ANSI_COLOR_RED "Monitor: No more items from %s\n" ANSI_COLOR_RESET, inet_ntoa(address.sin_addr));
                break;
            }
            printf("[Monitor server]: queue length -> %d, items produced -> %d", ntohl(message[0]), ntohl(message[1]));
            for (int i = 2; i < consumers_number + 2; i++) {
                printf(", [Consumer %d] -> %d", i - 2, ntohl(message[i]));
            }
            printf("\n");
        }
        /* The loop is most likely exited when the connection is terminated */
        printf(ANSI_COLOR_RED "Monitor: Connection terminated with %s\n" ANSI_COLOR_RESET, inet_ntoa(address.sin_addr));
        close(currentSocketDescriptor);
    }
    return 0; // never reached
}
