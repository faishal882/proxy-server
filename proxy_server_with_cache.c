#include "parse_request.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 10
#define MAX_BYTES 4096

typedef struct cache_element cache_element;
struct cache_element {
  char *data;
  int len;
  char *url;
  time_t lru_time_track;
  cache_element *next;
};

cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId;

pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

cache_element *head;
int cache_size;

int connectRemoteServer(char *host_addr, int port_num) {
  int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (remoteSocket < 0) {
    printf("[ERROR]: Error in creating your socket");
    return -1;
  }
  struct hostent *host = gethostbyname(host_addr);
  if (host == NULL) {
    fprintf(stderr, "[ERROR]: No such host Exist\n");
    return -1;
  }
  struct sockaddr_in server_addr;
  bzero((char *)&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_num);

  bcopy((char *)&host->h_addr, (char *)&server_addr.sin_addr.s_addr,
        host->h_length);
  if (connect(remoteSocket, (struct sockaddr *)&server_addr,
              (size_t)sizeof(server_addr)) < 0) {
    fprintf(stderr, "[ERROR]: Error in Connecting\n");
    return -1;
  }
  return remoteSocket;
}

int handle_request(int clientSocketId, struct ParsedRequest *request,
                   char *tempReq) {
  char *buff = (char *)malloc(sizeof(char) * MAX_BYTES);
  strcpy(buff, "GET");
  strcat(buff, request->path);
  strcat(buff, " ");
  strcat(buff, "\r\n");

  size_t len = strlen(buff);
  if (ParsedHeader_get(request, "Host") == NULL) {
    if (ParsedHeader_set(request, "Host", request->host) < 0) {
      printf("[ERROR]: Set Host header key is not working");
    }
  }

  if (ParsedRequest_unparse_headers(request, buff + len,
                                    (size_t)MAX_BYTES - len) < 0) {
    printf("[FAILED]: Unparsed failed");
  }

  int server_port = 80;
  if (request->port != NULL) {
    server_port = atoi(request->port);
  }
  int remoteSocketId = connectRemoteServer(request->host, server_port);
  if (remoteSocketId < 0) {
    return -1;
  }

  int bytes_send = send(remoteSocketId, buff, strlen(buff), 0);
  bzero(buff, MAX_BYTES);

  bytes_send = recv(remoteSocketId, buff, MAX_BYTES - 1, 0);
  char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES);
  int temp_buffer_size = MAX_BYTES;
  int temp_buffer_index = 0;

  while (bytes_send > 0) {
    bytes_send = send(clientSocketId, buff, bytes_send, 0);
    for (int i = 0; i < bytes_send / sizeof(char); i++) {
      temp_buffer[temp_buffer_index] = buff[i];
      temp_buffer_index++;
    }
    temp_buffer_size += MAX_BYTES;
    temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);
    if (bytes_send < 0) {
      fprintf(stderr, "[ERROR]: Error in sending data to client");
      break;
    }
    bzero(buff, MAX_BYTES);
    bytes_send = recv(remoteSocketId, buff, MAX_BYTES - 1, 0);
  }

  temp_buffer[temp_buffer_index] = '\0';
  free(buff);
  add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
  free(temp_buffer);
  close(remoteSocketId);

  return 0;
}

// TODO- NOT COMPLETED PASTE FROM GITHUB
int sendErrorMessage(int socket, int status_code) {
  char str[1024];
  char crrentTime[50];
  time_t now = time(0);

  switch (status_code) {
  case 500:
    snprintf(str, sizeof(str),
             "HTTP/1.1 500 Internal Server Error\r\nContent-Length..");
    printf("[ERROR]: Internal Server Error\n");
    send(socket, str, strlen(str), 0);
    break;
  }

  return -1;
}

int checkHTTPversion(char *msg) {
  int version = -1;
  if (strncmp(msg, "HTTP/1.1", 8) == 0) {
    version = 1;
  } else if (strncmp(msg, "HTTP/1.0", 8) == 0) {
    version = 1;
  } else
    version = -1;

  return version;
}

void *handle_thread(void *socketNew) {
  sem_wait(&semaphore);
  int p;
  sem_getvalue(&semaphore, &p);
  printf("[LOG]: Semaphore value is %d", p);

  int *t = (int *)socketNew;
  int _socket = *t;
  int bytes_send_client, len;

  char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
  bzero(buffer, MAX_BYTES);

  bytes_send_client = recv(_socket, buffer, MAX_BYTES, 0);
  while (bytes_send_client > 0) {
    len = strlen(buffer);
    if (strstr(buffer, "\r\n\r\n") == NULL) {
      bytes_send_client = recv(_socket, buffer + len, MAX_BYTES - len, 0);
    } else {
      break;
    }
  }

  char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
  for (int i = 0; i < strlen(buffer); i++) {
    tempReq[i] = buffer[i];
  }

  struct cache_element *temp = find(tempReq);
  if (temp != NULL) {
    int size = temp->len / sizeof(char);
    int pos = 0;
    char response[MAX_BYTES];
    while (pos < size) {
      bzero(response, MAX_BYTES);
      for (int i = 0; i < MAX_BYTES; i++) {
        response[i] = temp->data[i];
        pos++;
      }
      send(_socket, response, MAX_BYTES, 0);
    }
    printf("[SENT]: Data retrieved from cache\n");
    printf("%s\n\n", response);
  } else if (bytes_send_client > 0) {
    len = strlen(buffer);
    struct ParsedRequest *request = ParsedRequest_create();

    if (ParsedRequest_parse(request, buffer, len) < 0) {
      printf("[FAILED]: Parsing failed\n");
    } else {
      bzero(buffer, MAX_BYTES);
      if (!strcmp(request->method, "GET")) {
        if (request->host && request->path &&
            checkHTTPversion(request->version) == 1) {
          bytes_send_client = handle_request(_socket, request, tempReq);
          if (bytes_send_client == -1) {
            sendErrorMessage(_socket, 500);
          }
        } else {
          sendErrorMessage(_socket, 500);
        }
      } else {
        printf("[UNSUPPORTED]: This server support only GET request");
      }
    }
    ParsedRequest_destroy(request);
  } else if (bytes_send_client == 0) {
    printf("[DISCONNECTED]: Client is disconnected");
  }
  shutdown(_socket, SHUT_RDWR);
  close(_socket);
  free(buffer);
  sem_post(&semaphore);
  sem_getvalue(&semaphore, &p);
  printf("Semaphore value is %d", p);
  free(tempReq);

  return NULL;
}

int main(int argc, char *argv[]) {
  int client_socketId, client_len;
  struct sockaddr_in server_addr, client_addr;
  sem_init(&semaphore, 0, MAX_CLIENTS);

  pthread_mutex_init(&lock, NULL);

  if (argc == 2) {
    port_number = atoi(argv[1]);
  } else {
    printf("Too few argments\n");
    exit(1);
  }

  printf("[STARTING]: Starting Proxy Server at port: 8080");
  proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
  if (proxy_socketId < 0) {
    perror("[FAILED]: Failed to create socket\n");
    exit(1);
  }

  int reuse = 1;
  if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
                 sizeof(reuse)) < 0) {
    perror("[ERROR]: Failed to set setsockopt\n");
  }

  bzero((char *)&server_addr, sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_number);
  server_addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(proxy_socketId, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    perror("[ERROR]: Failed to bind\n");
    exit(1);
  }
  printf("[BIND]: Binding on port: %d\n", port_number);

  int listen_status = listen(proxy_socketId, MAX_CLIENTS);
  if (listen_status < 0) {
    perror("[ERROR]: Failed to listen\n");
    exit(1);
  }

  int i = 0;
  int connected_socketId[MAX_CLIENTS];

  while (1) {
    bzero((char *)&client_addr, sizeof(client_addr));
    client_len = sizeof(client_addr);
    client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr,
                             (socklen_t *)&client_len);
    if (client_socketId < 0) {
      perror("[ERROR]: Unable to connect\n");
      exit(1);
    } else {
      connected_socketId[i] = client_socketId;
    }

    struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
    struct in_addr ip_addr = client_pt->sin_addr;
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip_addr, str, INET6_ADDRSTRLEN);
    printf("[SUCCESS]: Client is connected to IP: %s and PORT: %d", str,
           ntohs(client_addr.sin_port));

    pthread_create(&tid, NULL, handle_thread, (void *)&connected_socketId[i]);
    i++;
  }

  close(proxy_socketId);
  return 0;
}
