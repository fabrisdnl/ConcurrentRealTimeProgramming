#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

#define BUFFER_SIZE 10
#define MESSAGES 1000       // Number of items produced by the producer
#define PRODUCER_MAX_WAIT 3E8 // Maximum random sleep time for a producer
#define CONSUMER_MAX_WAIT 1E9 // Maximum random sleep time for a consumer

/* Debug print colors */
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define FALSE 0
#define TRUE 1

/* Inter-process communication (IPC) */
pthread_mutex_t mutex;
pthread_cond_t write_condition, read_condition;
int buffer[BUFFER_SIZE];
int index_write = 0;
int index_read = 0;
char producing_completed = FALSE;
int items_produced = 0;
int* consumed;

/* Make sleep the current thread for a random amount of time between 0 and max_nanoseconds */
static inline void random_sleep(const long max_nanoseconds) {
    long nanoseconds = rand() % max_nanoseconds;
    static struct timespec sleep_time = {
        .tv_sec = 0
    };
    sleep_time.tv_nsec = nanoseconds;
    nanosleep(&sleep_time, NULL);
}

/* As long as the buffer is space in the buffer, it produces an item and place it in
   the buffer; then, it signals all the consumer threads a new item has been produced.
   When all the items has been produced, broadcasts all the consumers the production
   has been completed. */
static void* producer(void* arg) {
    for (int i = 0; i < MESSAGES; i++)
    {
        /* Simulate an item production */
        random_sleep(PRODUCER_MAX_WAIT);
        pthread_mutex_lock(&mutex);
        while ((index_write + 1) % BUFFER_SIZE == index_read) {
            pthread_cond_wait(&write_condition, &mutex);
        }
        #ifdef DEBUG
            printf(ANSI_COLOR_RED "[P ]: + %d\n" ANSI_COLOR_RESET, i);
        #endif
        /* Update the buffer */
        buffer[index_write] = i;
        index_write = (index_write + 1) % BUFFER_SIZE;
        /* Inform consumers a new item has been produced */
        pthread_cond_signal(&read_condition);
        pthread_mutex_unlock(&mutex);
        items_produced += 1;
    }
    pthread_mutex_lock(&mutex);
    producing_completed = TRUE;
    #ifdef DEBUG
        printf(ANSI_COLOR_RED "[Producer]: finished.\n" ANSI_COLOR_RESET);
    #endif
    pthread_cond_broadcast(&read_condition);
    pthread_mutex_unlock(&mutex);
    return NULL;
}

/* As long as there is one, consumes a message from the buffer, otherwise
   it waits until there is one or the producer has completed its work. Then, 
   inform the producer thread that the buffer is not full after it consumes. */
static void* consumer(void* arg) {
    const int id = *((int*)arg);
    free(arg);
    int item;
    while (TRUE) {
        pthread_mutex_lock(&mutex);
        while (!producing_completed && index_read == index_write) {
            pthread_cond_wait(&read_condition, &mutex);
        }
        if (producing_completed && index_read == index_write) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        item = buffer[index_read];
        index_read = (index_read + 1) % BUFFER_SIZE;
        consumed[id - 1] += 1;
        pthread_cond_signal(&write_condition);
        pthread_mutex_unlock(&mutex);
        #ifdef DEBUG
                printf(ANSI_COLOR_GREEN "[Consumer %d]: %d\n" ANSI_COLOR_RESET, id, item);
        #endif
        random_sleep(CONSUMER_MAX_WAIT);
    }
    return NULL;
}

/* Contains parameters for the server's monitor thread */
struct monitor_parameters
{
    int print_interval;
    int consumers_number;
    struct sockaddr_in server_address;
};

/* Reports to the server the queue length, the number of items produced and the number of
   items consumed by each consumer, at a given time interval. */
static void* monitor(void* arg) {
    const struct monitor_parameters* data = (struct monitor_parameters*)arg;
    const int print_interval = data->print_interval;
    const int consumers_number = data->consumers_number;
    const struct sockaddr_in server_address = data->server_address;
    /* Create a socket and connect it to the monitor server */
    const int socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor < 0) {
        perror("socket");
        exit(1);
    }
    if (connect(socketDescriptor, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        perror("connect");
        exit(1);
    }
    #ifdef DEBUG
        printf(ANSI_COLOR_BLUE "[Monitor thread]: Connected to monitor server\n" ANSI_COLOR_RESET);
    #endif
    /* Send number of consumer threads to the server */
    if (send(socketDescriptor, &consumers_number, sizeof(consumers_number), 0) < 0) {
        perror("send");
        exit(1);
    }
    while (TRUE) {
        pthread_mutex_lock(&mutex);
        const int queue_length = (index_write - index_read + BUFFER_SIZE) % BUFFER_SIZE;
        if (producing_completed && index_read == index_write) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
        #ifdef DEBUG
            printf(ANSI_COLOR_BLUE "Monitor: queue length -> %d, items produced -> %d,", queue_length, items_produced);
            for (int i = 0; i < consumers_number; i++) {
                printf(" [Consumer %d]: %d", i, consumed[i]);
            }
            printf("\n" ANSI_COLOR_RESET);
        #endif
        /* Prepare information to send to the server */
        int message[consumers_number + 2];
        message[0] = htonl(queue_length);
        message[1] = htonl(items_produced);
        for (int i = 0; i < consumers_number; i++) {
            message[i + 2] = htonl(consumed[i]);
        }
        /* Send message to the server */
        if (send(socketDescriptor, &message, sizeof(message), 0) < 0)
        {
            perror("send");
            exit(1);
        }
        sleep(print_interval);
    }
    /* Close the socket */
    close(socketDescriptor);
    return NULL;
}

int main(int argc, char* args[]) {
    if (argc != 5)
    {
        printf("Usage: <number of consumers: int> <server ip: char*> <port: int> <monitor printing interval: int [s]>\n");
        exit(1);
    }
    const int consumers_number = (int)strtol(args[1], NULL, 10);
    char ip[16];
    sscanf(args[2], "%15s", ip);
    const int port = (int)strtol(args[3], NULL, 10);

    /* Initialize IPC */
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&write_condition, NULL);
    pthread_cond_init(&read_condition, NULL);
    /* Create producer thread */
    pthread_t threads[consumers_number + 2];
    printf("Creating producer...\n");
    pthread_create(&threads[0], NULL, producer, NULL);
    /* Allocate memory */
    consumed = malloc(sizeof(int) * consumers_number);
    if (consumed == NULL) {
        perror("malloc");
        exit(1);
    }
    /* Create consumer threads */
    printf("Creating %d consumer threads...\n", consumers_number);
    for (int i = 1; i < consumers_number + 1; i++) {
        int* id = malloc(sizeof(*id));
        if (id == NULL) {
            perror("malloc");
            exit(1);
        }
        *id = i;
        pthread_create(&threads[i], NULL, consumer, id);
    }
    /* Fill the monitor parameters structure with information */
    struct monitor_parameters parameters = {
        print_interval: strtol(args[4], NULL, 10),
        consumers_number : consumers_number,
        server_address : {
            sin_family: AF_INET,
            sin_port : htons(port),
            sin_addr : {
                s_addr: inet_addr(ip)
            },
        },
    };
    printf("Creating monitor...\n");
    pthread_create(&threads[consumers_number + 1], NULL, monitor, &parameters);

    for (int i = 0; i < consumers_number + 1; i++) {
        pthread_join(threads[i], NULL);
    }
    free(consumed);
    return 0;
}