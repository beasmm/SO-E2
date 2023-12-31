#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "api.h"

#define BUFFER_SIZE 1024
#define ERROR -1

int session_number = 0;
int req_fd;
int res_fd;
char* req_pipe;
char* resp_pipe;

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

void createPipes() {
  if (unlink(req_pipe) != 0 && errno != ENOENT) {
      fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
      return 1;
  }

  if (mkfifo(req_pipe, 0666) != 0) {
      fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
      return 1;
  }

  if (unlink(resp_pipe) != 0 && errno != ENOENT) {
      fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
      return 1;
  }

  if (mkfifo(resp_pipe, 0666) != 0) {
      fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
      return 1;
  }

}

void closePipes() {
  if (unlink(req_pipe) != 0) {
      fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
      return 1;
  }

  if (unlink(resp_pipe) != 0) {
      fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
      return 1;
  }
}

void read_msg(int rx, char *buffer) {
    ssize_t ret = read(rx, buffer, BUFFER_SIZE - 1);

    if (ret == 0) {
        // ret == 0 indicates EOF
        fprintf(stderr, "[INFO]: pipe closed\n");
        exit(EXIT_FAILURE);
    } else if (ret == -1) {
        // ret == -1 indicates error
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "[INFO]: received %zd B\n", ret);
    buffer[ret] = 0;
    fputs(buffer, stdout);
}

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  //TODO: create pipes and connect to the server
  req_pipe = req_pipe_path;
  resp_pipe = resp_pipe_path;

  char server_pipe[BUFFER_SIZE];
  strcpy(server_pipe, "../"); 
  strcat(server_pipe, server_pipe_path);

  int tx = open(server_pipe, O_WRONLY);
  
  if (tx == -1) {
      fprintf(stderr, "[ERR]: first open failed: %s\n", strerror(errno));
      return 1;
  }

  createPipes();
  
  char buffer[BUFFER_SIZE];
  strcpy(buffer, req_pipe_path);
  strcat(buffer, " ");
  strcat(buffer, resp_pipe_path);
  strcat(buffer, " \n");
  send_msg(tx, buffer);

  //Open pipe to receive session id
  int rx = open(server_pipe, O_RDONLY);
  if (rx == -1) {
      fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
      return 1;
  }

  memset(buffer, 0, sizeof(buffer));
  ssize_t ret = read(rx, buffer, BUFFER_SIZE - 1);

  if (ret == 0) {
      // ret == 0 indicates EOF
      fprintf(stderr, "[INFO]: pipe closed\n");
      return 1;
  } else if (ret == -1) {
      // ret == -1 indicates error
      fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
      return 1;
  }

  fprintf(stderr, "[INFO]: received %zd B\n", ret);
  buffer[ret] = 0;
  fputs(buffer, stdout);

  session_number = atoi(buffer);
  close(rx);

  req_fd = open(req_pipe, O_WRONLY);
  if (req_fd == -1) {
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  res_fd = open(resp_pipe, O_RDONLY);
  if (res_fd == -1) {
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  return 0;
}

int ems_quit(void) { 
  if (unlink(req_pipe) != 0) {
      fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
      return 1;
  }

  if (unlink(resp_pipe) != 0) {
      fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
      return 1;
  }
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  //TODO: send create request to the server (through the request pipe) and wait for the response (through the response pipe)

  char buffer[BUFFER_SIZE];
  snprintf(buffer, sizeof(buffer), "3|%u|%ld|%ld\n", event_id, num_rows, num_cols);

  fprintf(stdout, "sent: %s\n", buffer);
  send_msg(req_fd, buffer);

// wait for response
  read_msg(res_fd, buffer);

  if(atoi(buffer) != 0) {
    fprintf(stdout, "Event not created\n");
    return 1;
  }

  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  //TODO: send reserve request to the server (through the request pipe) and wait for the response (through the response pipe)
  char buffer[BUFFER_SIZE];
  snprintf(buffer, sizeof(buffer), "4|%u|%ld", event_id, num_seats);

  for (int i = 0; i < num_seats; i++) {
    char temp[BUFFER_SIZE];
    snprintf(temp, sizeof(temp), "|%ld|%ld", xs[i], ys[i]);
    strcat(buffer, temp);
  }
  strcat(buffer, "\n");

  fprintf(stdout, "sent: %s\n", buffer);
  send_msg(req_fd, buffer);

  memset(buffer, 0, sizeof(buffer));
  read_msg(res_fd, buffer);

  if (atoi(buffer) != 0) {
    fprintf(stdout, "Seats not reserved\n");
    return 1;
  }

  return 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
   char buffer[BUFFER_SIZE];
  snprintf(buffer, sizeof(buffer), "5|%u\n", event_id);
  send_msg(req_fd, buffer);
  fprintf(stdout, "sent: %s\n", buffer);

  memset(buffer, 0, sizeof(buffer));
  ssize_t command = read(res_fd, buffer, BUFFER_SIZE - 1);
  if (command == 0) {
      fprintf(stderr, "[INFO]: pipe closed\n");
      return 1;
  } else if (command == -1) {
      fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
      return 1;
  }

  buffer[command] = 0;
  ssize_t ret = write(out_fd, buffer, strlen(buffer));
  if (ret < 0) {
    fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
    return 1;
  }
  return 0;
}

int ems_list_events(int out_fd) {
  //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
    char buffer[BUFFER_SIZE];
  snprintf(buffer, sizeof(buffer), "6\n");
  send_msg(req_fd, buffer);
  fprintf(stdout, "sent: %s\n", buffer);

   memset(buffer, 0, sizeof(buffer));
  ssize_t command = read(res_fd, buffer, BUFFER_SIZE - 1);
  if (command == 0) {
      fprintf(stderr, "[INFO]: pipe closed\n");
      return 1;
  } else if (command == -1) {
      fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
      return 1;
  }

  buffer[command] = 0;
  ssize_t ret = write(out_fd, buffer, strlen(buffer));
  if (ret < 0) {
    fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
    return 1;
  }
  return 0;
}
