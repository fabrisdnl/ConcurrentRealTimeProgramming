#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>

#define BUFFER_SIZE 16
#define MESSAGES 1024
#define MAX_WAIT 1E9

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define FALSE 0
#define TRUE 1

/*  IPC variables   */
pthread_mutex_t mutexLock;
pthread_cond_t conditionWait_write, conditionWait_read;
/*   indexes for buffer reading/writing   */
int write_pos = 0, read_pos = 0;

int buffer[BUFFER_SIZE];
int item = 0;

/*   number of messages produced   */
int msg_produced = 0;
/*  array of integers representing the number of messages consumed by each consumer */
int *msg_consumed;
/*  when the producer has produced the MESSAGES is set to TRUE  */
char completed = FALSE;

/*  monitor struct to supervise single producer multiple consumers  */
struct monitor {
    int time;
    int consumers;
    struct sockaddr_in server;
}

/*  the current thread stops and wait for a random value of nanoseconds*/
static void thread_sleep(const long max) {
    long nanoseconds = rand() % max;
    static struct timespec wait = {
        .tv_sec = 0,
        .tv_nsec = nanoseconds
    };
    nanosleep(&wait, NULL);
}

/*  producer code (passed argument is not used) */
static void *producer(void *arg) {
    for(int i = 0; i < MESSAGES; i++) {
        /*  simulating a message production */
        thread_sleep(MAX_WAIT);
        /*  entering critical section   */
        pthread_mutex_lock(&mutexLock);
        /*  waiting for room availability   */
        while((write_pos + 1) % BUFFER_SIZE == read_pos)
            pthread_cond_wait(&conditionWait_write, &mutexLock);
        /*  at this point room is available and we put the item in the buffer   */
        #ifdef DEBUG
            printf(ANSI_COLOR_CYAN "Producer: %d\n" ANSI_COLOR_RESET, i);
        #endif
        buffer[write_pos] = item;
        item++;
        msg_produced++;
        /*  after placing the item in the buffer, 
            write_pos index must be incremented   */
        write_pos = (write_pos + 1) % BUFFER_SIZE;
    }
    /*  inform all the consumers about the completed production */
    pthread_mutex_lock(&mutexLock);
    completed = TRUE;
    #ifdef DEBUG
        printf(ANSI_COLOR_CYAN "Producer: completed.\n" ANSI_COLOR_RESET);
    #endif
    pthread_cond_broadcast(&conditionWait_read);
    pthread_mutex_unlock(&mutexLock);
    return NULL;
}

/*  consumer code (passed argument: consumer identifier)*/
static void* consumer(void* arg) {
    int identifier = *((int*)arg);
    free(arg);
    int itemConsumed;
    while(TRUE) {
        pthread_mutex_lock(&mutexLock);
        /*  waiting for a new item to be available  */
        while (!completed && read_pos == write_pos)
            pthread_cond_wait(&conditionWait_read, &mutexLock);
        if (completed && read_pos == write_pos) {
            pthread_mutex_unlock(&mutexLock);
            break;
        }
        itemConsumed = buffer[read_pos];
        msg_consumed[identifier - 1] += 1;
        /*  after consuming the item in the buffer, 
            read_pos index must be incremented   */
        read_pos = (read_pos + 1) & BUFFER_SIZE;
        pthread_cond_signal(&conditionWait_write);
        pthread_mutex_unlock(&mutexLock);
        #ifdef DEBUG
            printf(ANSI_COLOR_MAGENTA "Consumer %d: consumed item %d\n" ANSI_COLOR_RESET,
                identifier, itemConsumed);
        #endif
        thread_sleep(MAX_WAIT);
    }
    return NULL;
}

static void* monitoring(void* arg) {
    /*  getting parameters of the monitor supervising 
        producer-consumers interaction  */
    struct monitor parameters = (struct monitor*)arg;
    int time = parameters.time;
    int consumers = parameters.consumers;
    struct sockaddr_in server = parameters.server;
    /*  creating new socket */
    int socketDescriptor;
    if ((socketDescriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket creation");
        exit(1);
    }
    if (connect(socketDescriptor,(struct sockaddr*)&server, sizeof(server)) == -1) {
        perror("connect");
        exit(1);
    }
    #ifdef
        printf(ANSI_COLOR_YELLOW "Monitor: Connected to server.\n" ANSI_COLOR_RESET);
    #endif
    /*  Sending first the nuumber of consumers  */
    if (send(socketDescriptor, &consumers, sizeof(consumers), 0) == -1) {
        perror("send");
        exit(1);
    }
    while(TRUE) {
        pthread_mutex_lock(&mutexLock);
        int queueLen = (write_pos - read_pos + BUFFER_SIZE) % BUFFER_SIZE;
        if (completed && read_pos == write_pos) {
            pthread_mutex_unlock(&mutexLock);
            break;
        }
        pthread_mutex_unlock(&mutexLock);
        #ifdef DEBUG
            printf(ANSI_COLOR_YELLOW "Monitor: [queue: %d], [produced: %d]",
                     queueLen, consumed);
            for (int j = 0; j < consumers; j++) {
                printf("[%d]: %d", j, msg_consumed[j]);
            }
            printf("\n" ANSI_COLOR_RESET);
        #endif
        /*  preparing information to send to the server */
        int msg[consumers + 2];
        msg[0] = htonl(queueLen);
        msg[1] = htonl(msg_produced);
        for (int k = 0; k < consumers; k++) {
            /*  number of consumed items by each consumer   */
            msg[k + 2] = htonl(msg_consumed[k]);
        }
        /*  Sending the message to the server   */
        if (send(socketDescriptor, &msg, sizeof(msg), 0) == -1) {
            perror("send");
            exit(1);
        }
        thread_sleep(time);
    }
    close(socketDescriptor);
    return NULL;
}

/*  receive routine */
static int receive(int socketDescriptor, char *retBuffer, int size) {
    int totalSize = 0, currentSize;
    while (totalSize < size) {
        currentSize = recv(socketDescriptor, &retBuffer[totalSize], size - totalSize, 0);
        if (currentSize <= 0)
            return -1; // an error has occurred
        totalSize += currentSize;
    }
    return 0;
}

/*  main function   */
int main(int argc, char **argv) {
    char hostname[100], command[256];
    char *answer;
    int socketDescriptor, port, stopped = FALSE, len;
    unsigned int networkLen;
    struct sockaddr_in socketAddress;
    struct hostent *host;
    /*  getting IP address and port number  */
    if (argc < 3) {
        printf("usage: client <hostname> <port>\n");
        exit(0);
    }
    sscanf(argv[1], "%s", hostname);
    sscanf(argv[2], "%d", &port);
    /*  resolve the passed name and store the resulting long
        representation in the struct hostent variable   */
    if ((host = gethostbyname(hostname)) == 0) {
        perror("gethostbyname");
        exit(1);
    }
    /*  fill in the socket structure with the host information  */
    memset(&socketAddress, 0, sizeof(socketAddress));
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_addr.s_addr = ((struct in_addr *)(host -> h_addr_list[0])) -> s_addr;
    socketAddress.sin_port = htons(port);
    /*  create a new socket */
    if ((socketDescriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    /*  connect the socket to the port and host specified in struct sockaddr_in */
    if (connect(socketDescriptor, (struct sockaddr *)&socketAddress,
        sizeof(socketAddress)) == -1) {
        perror("connect");
        exit(1);
    }

    while(!stopped) {
        /*  get a string command from terminal  */
        printf("enter command: ");
        scanf("%s", command);
        if (!strcmp(command, "quit")) break;
        len = strlen(command); // send first the number of characters in the command
        networkLen = htonl(len); // converting integer number into network byte order
        /*  sending the number of charachters in the command    */
        if (send(socketDescriptor, &networkLen, sizeof(networkLen), 0) == -1) {
            perror("send");
            exit(1);
        }
        /*  sending the command */
        if (send(socketDescriptor, command, len, 0) == -1) {
            perror("send");
            exit(1);
        }
        /*  receive the answer: (number of characters, answer)  */
        if (receive(socketDescriptor, (char *)&networkLen, sizeof(networkLen))) {
            perror("recv");
            exit(1);
        }
        /*  convert from network byte order */
        len = ntohl(networkLen);
        /*  allocate and receive the answer */
        answer = malloc(len + 1);
        if (receive(socketDescriptor, answer, len)) {
            perror("recv");
            exit(1);
        }
        answer[len] = 0;
        printf("%s\n", answer);
        free(answer);
        if (!strcmp(command, "stop"))
            break;
    }
    close(socketDescriptor);
    return 0;
}