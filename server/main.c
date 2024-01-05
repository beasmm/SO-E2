#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

#define BUFFER_SIZE 1024
#define MAX_SESSIONS 10

int active_events[MAX_SESSIONS];
int _index = 0;

//SIG
volatile sig_atomic_t sigurs1_detected = 0;
int open_in_progress = 0;


pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

typedef struct {
  char queue[MAX_SESSIONS][514];
  int front, rear, count;
} Queue;

Queue producer_consumer; 
char pipe_name[BUFFER_SIZE];

void signal_handler(int signum){
  sigurs1_detected = 1;
  while (open_in_progress) sleep(1000);
}

void initializeQueue() {
  producer_consumer.front = 0;
  producer_consumer.rear = -1;
  producer_consumer.count = 0;
}

void enqueue(char *buf) {
  pthread_mutex_lock(&mutex);
  while (producer_consumer.count == MAX_SESSIONS) {
      pthread_cond_wait(&cond, &mutex);
  }
  producer_consumer.rear = (producer_consumer.rear + 1) % MAX_SESSIONS;
  strcpy(producer_consumer.queue[producer_consumer.rear], buf);
  producer_consumer.count++;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);
}


char* dequeue() {
  pthread_mutex_lock(&mutex);
  while (producer_consumer.count == 0) {
      pthread_cond_wait(&cond, &mutex);
  }
  char* msg = producer_consumer.queue[producer_consumer.front];
  producer_consumer.front = (producer_consumer.front + 1) % MAX_SESSIONS;
  producer_consumer.count--;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);
  return msg;
}

enum OP_TYPE {
  OP_CREATE,
  OP_RESERVE,
  OP_SHOW,
  OP_LIST_EVENTS,
  OP_WAIT,
  OP_INVALID
} op_type;

enum OP_TYPE getOperation (char* command) {
  if (!strcmp(command, "3")) return OP_CREATE;
  else if (!strcmp(command, "4")) return OP_RESERVE;
  else if (!strcmp(command, "5")) return OP_SHOW;
  else if (!strcmp(command, "6\n")) return OP_LIST_EVENTS;
  else if (!strcmp(command, "WAIT\n")) return OP_WAIT;
  else return OP_INVALID;
}

char** seperateElements(char* command) {
  char** elements = malloc(sizeof(char*) * 10);
  char* token = strtok(command, "|");
  int i = 0;
  while (token != NULL) {
    elements[i] = token;
    i++;
    token = strtok(NULL, "|");
  }
  return elements;
}

void send_msg(int tx, char const *str) {
    size_t len = strlen(str);
    size_t written = 0;

    while (written < len) {
        ssize_t ret = write(tx, str + written, len - written);
        if (ret < 0) {
            fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        fprintf(stdout, "sent: %s\n", str);
        written += ret;
    }
}

void* executeRequest(void* arg) {
  // Block SIGUSR1 in the worker threads
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);


  char req_pipe[BUFFER_SIZE];
  char buffer[BUFFER_SIZE];
  strcpy(buffer, dequeue());

  strcpy(req_pipe, "../client/");
  strcat(req_pipe, strtok(buffer, " "));
  char resp_pipe[BUFFER_SIZE];
  strcpy(resp_pipe, "../client/");
  strcat(resp_pipe, strtok(NULL, " "));
  

  char response[BUFFER_SIZE];
  snprintf(response, sizeof(response), "%d", producer_consumer.count + 1);
  strcat(response, "\n");

  //Open pipe to write the session id
  open_in_progress = 1;
  int tx = open(pipe_name, O_WRONLY);
  if (tx == -1) {
      fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }
  send_msg(tx, response);   

  close(tx);

  // Open request pipe to read commands
  int rx = open(req_pipe, O_RDONLY);
  if (rx == -1) {
      fprintf(stderr, "[ERR]: open req failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }

  int resp = open(resp_pipe, O_WRONLY);
  if (resp == -1) {
      fprintf(stderr, "[ERR]: open resp failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }

  while (1) {
    memset(response, 0, sizeof(response));
    ssize_t command = read(rx, buffer, BUFFER_SIZE - 1);
    if (command == 0) {
        fprintf(stderr, "[INFO]: pipe closed\n");
        break;
    } else if (command == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return 1;
    }

    fprintf(stderr, "[INFO]: received %zd B\n", command);
    buffer[command] = 0;
    fputs(buffer, stdout);

    char** elements = seperateElements(buffer);
    int event_id, ret;
    size_t num_rows, num_cols, num_coords;
    char* endptr;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
    char buffer[BUFFER_SIZE-10];

    switch (getOperation(elements[0])) {
      case OP_CREATE:
        event_id = atoi(elements[1]);
        num_rows = strtoul(elements[2], &endptr, 10);
        num_cols = strtoul(elements[3], &endptr, 10);

        ret = ems_create(event_id, num_rows, num_cols);

        if (ret != 0) fprintf(stderr, "Failed to create event\n");
        snprintf(response, sizeof(response), "%d\n", ret);
        break;
      
      case OP_RESERVE:
        event_id = atoi(elements[1]);
        num_coords = strtoul(elements[2], &endptr, 10);

        for (int i = 0; i < num_coords; i++) {
          xs[i] = strtoul(elements[3 + 2*i], &endptr, 10);
          ys[i] = strtoul(elements[4 + 2*i], &endptr, 10);
        }

        ret = ems_reserve(event_id, num_coords, xs, ys);

        if (ret != 0) fprintf(stderr, "Failed to reserve seats\n");
        snprintf(response, sizeof(response), "%d\n", ret);

        break;

      case OP_SHOW:
        event_id = atoi(elements[1]);

        ret = ems_show(resp, event_id, buffer);
        if (ret != 0) fprintf(stderr, "Failed to show event\n");
        snprintf(response, sizeof(response), "%d|%s\n", ret, buffer);
        break;

      case OP_LIST_EVENTS:
        ret = ems_list_events(resp, buffer);
        if (ret != 0) fprintf(stderr, "Failed to list events\n");
        snprintf(response, sizeof(response), "%d|%s\n", ret, buffer);
        break;
      
      case OP_WAIT:
        unsigned int delay = strtoul(elements[1], &endptr, 10);
        if (delay > 0) {
          printf("Waiting...\n");
          sleep(delay);
        }
        break;

      case OP_INVALID:
        break;

      default:
        break;
    }
  
    send_msg(resp, response);

  }
  close(rx);
  close(resp);

  open_in_progress = 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  strcpy(pipe_name, "../");
  strcat(pipe_name, argv[1]);

  int n_sessions = 0;

  //Creates pipe (register fifo)
  if (unlink(pipe_name) != 0 && errno != ENOENT) {
      fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
      return 1;
  }

  if (mkfifo(pipe_name, 0640) != 0) {
      fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
      return 1;
  }

  // Initialize the producer-consumer queue
  initializeQueue();

  // Create worker threads
  pthread_t worker_threads[MAX_SESSIONS];
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (pthread_create(&worker_threads[i], NULL, executeRequest, NULL) != 0) {
      fprintf(stderr, "error creating thread.\n");
      return -1;
    }
  }
  //TODO; eventual locks
  open_in_progress = 0;
  while (1) {
    if (sigurs1_detected == 1){
      fprintf(stdout, "SIGUSR1 detected. Now showing active events:\n");
      for (int i = 0; i < MAX_SESSIONS; i++){
        int id = active_events[_index];
        if (id == -1) continue;
        fprintf(stdout, "ID: %d. Current state of seats:\n", active_events[_index]);
        char buf[BUFFER_SIZE];
        ems_show(stdout, id, buf);
        sigurs1_detected = 0;
      }
    }
    open_in_progress = 1;
    int r_register_pipe = open(pipe_name, O_RDONLY);
    if (r_register_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return 1;
    }
    char buffer[BUFFER_SIZE];
    //Read from pipe
    fprintf(stdout, "[INFO]: waiting for input\n");
    ssize_t ret = read(r_register_pipe, buffer, BUFFER_SIZE - 1);
    if (ret == 0) {
      close(r_register_pipe);
      open_in_progress = 0;
      continue;
    } else if (ret == -1) {
      fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
      return 1;
    }
    printf("cllo\n");

    fprintf(stderr, "[INFO]: received %zd B\n", ret);
    buffer[ret] = 0;
    fputs(buffer, stdout); 

    enqueue(buffer);

    close(r_register_pipe);
    open_in_progress = 0;
  }

  //TODO: Close Server
  ems_terminate();
  return 0;
}