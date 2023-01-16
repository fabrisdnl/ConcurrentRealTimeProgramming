#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

#define FALSE 0
#define TRUE 1

static int receive(int sd, char *retBuf, int size) {
    int totSize, currSize;
    totSize = 0;
    while(totSize < size) {
        currSize = recv(sd, &retBuf[totSize], size - totSize, 0);
        if(currSize <= 0)
        /* An error occurred */
        return -1;
        totSize += currSize;
    }
    return 0;
}

/* Main function */
int main(int argc, char *argv[]) {
    int socketDescriptor;
    if(argc < 2) {
        printf("Usage: <server port>\n");
        exit(0);
    }
    const int port = strtol(argv[1], NULL, 10);
    struct sockaddr_in server_address = {
        sin_family: AF_INET,
        sin_port : htons(port),
        sin_addr : {
            s_addr: INADDR_ANY
        },
    };

    /* Create a new socket */
    if ((socketDescriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    /* Bind the socket to the specified port number */
    if (bind(socketDescriptor, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        perror("bind");
        exit(1);
    }
    /* Set the maximum queue length for clients requesting connection to 5 */
    if (listen(socketDescriptor, 5) == -1) {
        perror("listen");
        exit(1);
    }
    /* Accept and serve all incoming connections in a loop */
    while(TRUE) {
        struct sockaddr_in address;
        int addressLength = sizeof(address);
        int currentSocketDescriptor;
        if ((currentSocketDescriptor = accept(socketDescriptor, (struct sockaddr*)&address, (socklen_t*)&addressLength)) == -1) {
            perror("accept");
            exit(1);
        }
        /* When execution reaches this point a client established the connection.
        The returned socket (currSd) is used to communicate with the client */
        printf("Connection received from %s\n", inet_ntoa(address.sin_addr));
        
        /* As first message receive the number of consumers */
        int consumers_number = 0;
        if (receive(currentSocketDescriptor, (char*)&consumers_number, sizeof(int)) == -1){
            perror("receive");
            exit(1);
        }
        printf("[Monitor]: Correctly received number of consumers %d\n", consumers_number);
        int message[consumers_number + 2];

        while(TRUE) {
            if (receive(currentSocketDescriptor, (char*)&message, sizeof(message)) == -1) {
                perror("receive");
                break;
            }
            printf("[Monitor server]: queue: %d, produced: %d", ntohl(message[0]), ntohl(message[1]));
            for (int i = 2; i < consumers_number + 2; i++)
            {
                printf(", [Consumer %d]: %d", i - 2, ntohl(message[i]));
            }
            printf("\n");
        }
        /* The loop is most likely exited when the connection is terminated */
        printf("Connection terminated\n");
        close(currentSocketDescriptor);
    }
    return 0; // never reached
}