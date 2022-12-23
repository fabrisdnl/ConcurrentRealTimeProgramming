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

/*  reads a message from the socket and stores it to the buffer specified   */
static int receive(int socketDescriptor, char *retBuffer, int size) {
    int totalSize = 0, currentSize;
    while (totalSize < size) {
        /*  recv shall return the length of the message written to the buffer
            pointed to by the buffer argument, the value 0 indicates the connection 
            is closed; parameters:
            - socket file descriptor
            - buffer where the message should be stored
            - length in bytes of the buffer pointed to by the buffer parameter */
        currentSize = recv(socketDescriptor, &retBuffer[totalSize], size - totalSize, 0);
        if (currentSize <= 0) {
            /*   An error occurred  */
            return -1;
        }
        totalSize += currentSize;
    }
    /*  connection closed   */
    return 0;
}

/*  handle an established connection (information are printed here) */
static void handleConnection(int currentSocketDescriptor) {
    unsigned int addressLength;
    int len, exit_status = 0;
    char *message, *answer;

    /* expecting number of consumers as first message   */
    int consumers = 0;
    if (receive(currentSocketDescriptor, (char*)&consumers, sizeof(int)) == -1) {
        perror("receive");
        exit(1);
    }
    printf("supervisor server: [number of consumers: %d]\n", consumers);
    int msg[consumers + 2];

    for(;;) {
        if (receive(currentSocketDescriptor, (char*)&msg, sizeof(msg)) == -1)
            break;
        /*  ntohl shall return the argument value converted from 
            network to host byte order   */
        printf("supervisor server:\n[queue: %d], [produced: %d]",
                ntohl(msg[0]), ntohl(msg[1]));
        for (int i = 2; i < consumers + 2; i++) {
            printf(", [%d]: %d", i - 2, ntohl(msg[i]));
        }
        printf("\n");
    }
    /*  exiting from the loop when the connection is ended  */
    printf("connection terminated\n");
    close(currentSocketDescriptor);
}

/*  main program    */
int main(int argc, char *argv[]) {
    int socketDescriptor, currentSocketDescriptor;
    int socketAddressLength, port;
    int len;
    unsigned int addressLength;
    char *command, *answer;
    struct sockaddr_in socketAddress, clientSocketAddress;
    
    /*  reading server port number  */
    if (argc < 2) {
        printf("usage: server <port>\n");
        exit(0);
    }
    sscanf(argv[1], "%d", &port);
    /*  creating a new socket   */
    if ((socketDescriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket creation");
        exit(1);
    }
    /*  setting socket options REUSE ADDRESS    */
    int reuse = 1;
    if (setsockopt(socketDescriptor, SOL_SOCKET, SO_REUSEADDR,
        (const char*)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }
    #ifdef SO_REUSEPORT
    if (setsockopt(socketDescriptor, SOL_SOCKET, SO_REUSEPORT,
        (const char*)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEPORT) failed");
    }
    #endif
    /*  Initialize the address (struct sockaddr_in) fields  */
    memset(&socketAddress, 0, sizeof(socketAddress)); // sin to all zeros
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_addr.s_addr = INADDR_ANY;
    /*  htons shall return the argument value converted from (unsigned short) 
        host to network byte order   */
    socketAddress.sin_port = htons(port);

    /*  bind the socket to the specified port number    */
    if (bind(socketDescriptor, (struct sockaddr*)&socketAddress,
        sizeof(socketAddress)) == -1) {
        perror("bind");
        exit(1);
    }
    /* set the maximum queue length for clients requesting connection to 5  */
    if (listen(socketDescriptor, 5) == -1) {
        perror("listen");
        exit(1);
    }

    socketAddressLength = sizeof(clientSocketAddress);
    /*  accept and serve all incoming connections in a loop */
    for(;;) {
        printf("supervisor server: [status: READY]");
        if ((currentSocketDescriptor = accept(socketDescriptor,
            (struct sockaddr*)&clientSocketAddress, &socketAddressLength)) == -1) {
            perror("accept");
            exit(1);
        }
        /*  when the execution reaches this point a client has established the
            connection; the return socket (currentSocketDescriptor) is used
            to communicate with the client  */
        /*  inet_ntoa returns the dots and numbers string in a static buffer that
            is overwritten with eaach call to the function  */
        printf("connection received from %s\n", inet_ntoa(clientSocketAddress.sin_addr));
        handleConnection(currentSocketDescriptor);
    }
    /*  unreachable */
    return 0;
}