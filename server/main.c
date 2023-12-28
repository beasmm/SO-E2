#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "client/parser.h" //can i do this?

#define BUFFER_SIZE 1024
#define MAX_SESSIONS 10

enum Command getCommand(char* command) {
  if (!strcmp(command, "CREATE")) return CMD_CREATE;
  else if (!strcmp(command, "RESERVE")) return CMD_RESERVE;
  else if (!strcmp(command, "SHOW")) return CMD_SHOW;
  else if (!strcmp(command, "LIST")) return CMD_LIST_EVENTS;
  else if (!strcmp(command, "WAIT")) return CMD_WAIT;
  else return CMD_INVALID;
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

        written += ret;
    }
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

  char const pipe_name[BUFFER_SIZE];
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

  int r_register_pipe = open(pipe_name, O_RDONLY);
  if (r_register_pipe == -1) {
      fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
      return 1;
  }

  // while (1) {
    //Read from pipe
    char buffer[BUFFER_SIZE];
    fprintf(stdout, "[INFO]: waiting for input\n");
    ssize_t ret = read(r_register_pipe, buffer, BUFFER_SIZE - 1);
    if (ret == 0) {
        // ret == 0 indicates EOF
        fprintf(stderr, "[INFO]: pipe closed\n");
        return 0;
    } else if (ret == -1) {
        // ret == -1 indicates error
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    //TODO: Intialize server, create worker threads
    //how many do i need? and what functions do i need to call?
    // pthread_t tid;
    // if (pthread_create(&tid, NULL, thr_func, NULL) != 0) {
    //     fprintf(stderr, "error creating thread.\n");
    //     return -1;
    // }

    fprintf(stderr, "[INFO]: received %zd B\n", ret);
    buffer[ret] = 0;
    // fputs(buffer, stdout); 

    char req_pipe[BUFFER_SIZE];
    strcpy(req_pipe, "../client/");
    strcat(req_pipe, strtok(buffer, " "));
    char resp_pipe[BUFFER_SIZE];
    strcpy(resp_pipe, "../client/");
    strcat(resp_pipe, strtok(NULL, " "));
    
    //TODO: Write new client to the producer-consumer buffer

    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "%d", n_sessions);
    strcat(response, "\n");
    
    int tx = open(pipe_name, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    send_msg(tx, response);
    n_sessions++;
    close(tx);

    int rx = open(req_pipe, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    while (1) {
      fprintf(stdout, "read command\n");
      ssize_t command = read(rx, buffer, BUFFER_SIZE - 1);
      if (command == 0) {
          // ret == 0 indicates EOF
          fprintf(stderr, "[INFO]: pipe closed\n");
          break;
      } else if (command == -1) {
          // ret == -1 indicates error
          fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
          return 1;
      }

      fprintf(stderr, "[INFO]: received %zd B\n", command);
      buffer[command] = 0;
      fputs(buffer, stdout);

      char* token = strtok(buffer, " ");

      switch (getCommand(token)) {
        case CMD_CREATE:
          int event_id = atoi(strtok(NULL, " "));
          char *endptr;
          size_t num_rows = strtoul(strtok(NULL, " "), &endptr, 10);
          size_t num_cols = strtoul(strtok(NULL, "\n"), &endptr, 10);

          if (ems_create(event_id, num_rows, num_cols) != 0)
            fprintf(stderr, "Failed to create event\n");
          break;
        
        case CMD_RESERVE:
          event_id = atoi(strtok(NULL, " "));
          size_t num_coords = 0;
          size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
          char* token = strtok(NULL, " ");
          while (token != NULL) {
            xs[num_coords] = atoi(token);
            ys[num_coords] = atoi(strtok(NULL, " "));
            num_coords++;
            token = strtok(NULL, " ");
          }

          if (ems_reserve(event_id, num_coords, xs, ys) != 0)
            fprintf(stderr, "Failed to reserve seats\n");
          break;

        default:
          break;
      }

    }

  // }

  //TODO: Close Server
  close(r_register_pipe);
  ems_terminate();
}