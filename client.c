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

#define BUFFER_SIZE 128
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
/*   indexes for reading/writing buffer's items */
int write_pos = 0, read_pos = 0;

int buffer[BUFFER_SIZE];

/*   number of messages produced   */
int msg_produced = 0;
/*  array of integers representing the number of messages consumed by each consumer */
int *msg_consumed;
/*  when the producer has produced the MESSAGES is set to TRUE  */
char producingCompleted = FALSE;

/*  monitor struct to supervise single producer multiple consumers activity */
struct monitor {
    int time;
    int consumers;
    struct sockaddr_in server;
};

/*  the current thread stops and wait (sleep) for a random value of nanoseconds */
static void thread_sleep(const long max) {
    const long nanoseconds = rand() % max;
    static struct timespec wait = {
        .tv_sec = 0
    };
    wait.tv_nsec = nanoseconds;
    nanosleep(&wait, NULL);
}

/*  producer code (passed argument is not used) */
static void *producer(void *arg) {
    for(int i = 0; i < MESSAGES; i++) {
        /*  simulating a message production making thread sleeping  */
        thread_sleep(MAX_WAIT);
        /*  entering critical section   */
        pthread_mutex_lock(&mutexLock);
        /*  waiting for room availability   */
        while((write_pos + 1) % BUFFER_SIZE == read_pos) {
            pthread_cond_wait(&conditionWait_write, &mutexLock);
        }
        /*  at this point room is available and we put the item in the buffer   */
        #ifdef DEBUG
            printf(ANSI_COLOR_CYAN "Producer: %d\n" ANSI_COLOR_RESET, i);
        #endif
        buffer[write_pos] = i;
        msg_produced++;
        /*  after placing the item in the buffer, 
            write_pos index must be incremented   */
        write_pos = (write_pos + 1) % BUFFER_SIZE;
        /*  inform all the consumers a new message has been produced    */
        pthread_cond_signal(&conditionWait_read);
        /*  leaving critical section    */
        pthread_mutex_unlock(&mutexLock);
    }
    /*  inform all the consumers about the completed production */
    pthread_mutex_lock(&mutexLock);
    producingCompleted = TRUE;
    #ifdef DEBUG
        printf(ANSI_COLOR_CYAN "Producing: completed.\n" ANSI_COLOR_RESET);
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
        /*  entering critical section   */
        pthread_mutex_lock(&mutexLock);
        /*  waiting for a new item to be available  */
        while (!producingCompleted && read_pos == write_pos)
            pthread_cond_wait(&conditionWait_read, &mutexLock);
        if (producingCompleted && read_pos == write_pos) {
            pthread_mutex_unlock(&mutexLock);
            break;
        }
        itemConsumed = buffer[read_pos];
        /*  incrementing consumer's item consumed counter   */
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
    struct monitor* parameters = (struct monitor*)arg;
    int time = parameters->time;
    int consumers = parameters->consumers;
    struct sockaddr_in server = parameters->server;
    /*  creating new socket */
    int socketDescriptor;
    if ((socketDescriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket creation");
        exit(1);
    }
    if (connect(socketDescriptor,(struct sockaddr*)&server, sizeof(server)) == -1) {
        perror("socket connection");
        exit(1);
    }
    #ifdef DEBUG
        printf(ANSI_COLOR_YELLOW "Monitor: Connected to server.\n" ANSI_COLOR_RESET);
    #endif
    /*  Sending first the number of consumers  */
    if (send(socketDescriptor, &consumers, sizeof(consumers), 0) == -1) {
        perror("sending number of consumers");
        exit(1);
    }
    while(TRUE) {
        /*  entering critical section   */
        pthread_mutex_lock(&mutexLock);
        int queueLen = (write_pos - read_pos + BUFFER_SIZE) % BUFFER_SIZE;
        if (producingCompleted && read_pos == write_pos) {
            pthread_mutex_unlock(&mutexLock);
            break;
        }
        pthread_mutex_unlock(&mutexLock);
        #ifdef DEBUG
            printf(ANSI_COLOR_YELLOW "Monitor: [queue: %d], [produced: %d],",
                     queueLen, consumed);
            for (int j = 0; j < consumers; j++) {
                printf(" [%d]: %d", j, msg_consumed[j]);
            }
            printf("\n" ANSI_COLOR_RESET);
        #endif
        int msg[consumers + 2];
        msg[0] = htonl(queueLen);
        msg[1] = htonl(msg_produced);
        for (int k = 0; k < consumers; k++) {
            /*  number of consumed items by each consumer   */
            msg[k + 2] = htonl(msg_consumed[k]);
        }
        /*  Sending the information to the server   */
        if (send(socketDescriptor, &msg, sizeof(msg), 0) == -1) {
            perror("sending information to server");
            exit(1);
        }
        thread_sleep(time);
    }
    /*  closing socket  */
    close(socketDescriptor);
    return NULL;
}

/*  main function   */
int main(int argc, char **argv) {
    char monitorServerAddress[16];
    int port, consumers, monitor_time;
    /*  getting input from command line arguments   */
    if (argc < 5) {
        printf("usage: %s <number of consumers: [int]> <monitor server address: [char*]>"
                " <monitor server port: [int]> <monitor scan period (s): [int]>\n",
                 argv[0]);
        exit(0);
    }
    consumers = (int)strtol(argv[1], NULL, 10);
    sscanf(argv[2], "%15s", monitorServerAddress);
    port = (int)strtol(argv[3], NULL, 10);
    monitor_time = strtol(argv[4], NULL, 10);
    /*  initialize mutex and condition wait global variables    */
    pthread_mutex_init(&mutexLock, NULL);
    pthread_cond_init(&conditionWait_read, NULL);
    pthread_cond_init(&conditionWait_write, NULL);
    /* producer thread  */
    pthread_t threads[consumers + 2]; // + producer + monitor
    printf("producer thread creation...\n");
    pthread_create(&threads[0], NULL, producer, NULL);
    /*  allocate the memory in advance to track consumed items for each consumer    */
    msg_consumed = malloc(sizeof(int) * consumers);
    if (msg_consumed == NULL) {
        perror("malloc");
        exit(1);
    }
    /*  consumer threads    */
    printf("consumer threads creation...\n");
    for (int i = 1; i <= consumers; i++) {
        int* identifier = malloc(sizeof(*identifier));
        if (identifier == NULL) {
            perror("malloc");
            exit(1);
        }
        *identifier = i;
        pthread_create(&threads[i], NULL, consumer, identifier);
    }
    /*  setting monitor's parameters    */
    struct monitor parameters = {
        time: monitor_time,
        consumers: consumers,
        server: {
            sin_family: AF_INET,
            sin_port: htons(port),
            sin_addr : {
                s_addr: inet_addr(monitorServerAddress)
            },
        },
    };

    pthread_create(&threads[consumers + 1], NULL, monitoring, &parameters);
    printf("monitor started\n");
    for (int i = 0; i < consumers + 1; i++) {
        pthread_join(threads[i], NULL);
    }
    free(msg_consumed);
    return 0;
}