#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <time.h>

#define DB_SIZE 10
#define MAX_CLIENTS 50
#define BUFFER_SIZE 256

int db[DB_SIZE];
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
int active_writers = 0;
pthread_mutex_t rw_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t writer_cond = PTHREAD_COND_INITIALIZER;
int monitor_sock = -1;
pthread_mutex_t monitor_mutex = PTHREAD_MUTEX_INITIALIZER;

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl failed");
    }
}

void set_socket_buffers(int sock) {
    int buf_size = 65536;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
        perror("Set receive buffer failed");
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0) {
        perror("Set send buffer failed");
    }
}

void send_to_monitor(int client_sock, const char *msg) {
    pthread_mutex_lock(&monitor_mutex);
    if (monitor_sock != -1 && monitor_sock != client_sock) {
        printf("Server: Sending to monitor: '%s'", msg);
        if (send(monitor_sock, msg, strlen(msg), 0) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Failed to send to monitor");
                monitor_sock = -1;
            }
        }
    }
    pthread_mutex_unlock(&monitor_mutex);
}

void send_db_state(int client_sock) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "DB [");
    for (int i = 0; i < DB_SIZE; i++) {
        char temp[16];
        snprintf(temp, sizeof(temp), "%d", db[i]);
        strncat(buffer, temp, sizeof(buffer) - strlen(buffer) - 1);
        if (i < DB_SIZE - 1) strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
    }
    strncat(buffer, "]\n", sizeof(buffer) - strlen(buffer) - 1);
    send_to_monitor(client_sock, buffer);
}

void *handle_client(void *arg) {
    int client_sock = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int valread;

    set_nonblocking(client_sock);
    set_socket_buffers(client_sock);
    printf("Client connected: sock %d\n", client_sock);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

        int ready = select(client_sock + 1, &read_fds, NULL, NULL, &tv);
        if (ready < 0) {
            perror("Select error");
            break;
        }
        if (ready == 0) {
            continue;
        }

        valread = read(client_sock, buffer, sizeof(buffer) - 1);
        if (valread <= 0) {
            if (valread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Read error");
            } else {
                printf("Client disconnected: sock %d\n", client_sock);
            }
            break;
        }

        buffer[valread] = '\0';
        printf("Server: Received from client %d: '%s'\n", client_sock, buffer);

        if (strcmp(buffer, "MONITOR") == 0) {
            pthread_mutex_lock(&monitor_mutex);
            monitor_sock = client_sock;
            pthread_mutex_unlock(&monitor_mutex);
            send_db_state(client_sock);
            printf("Monitor registered: sock %d\n", client_sock);
            continue;
        }

        if (strncmp(buffer, "READ", 4) == 0) {
            pthread_mutex_lock(&db_mutex);
            int index = rand() % DB_SIZE;
            int value = db[index];
            pthread_mutex_unlock(&db_mutex);

            printf("Reader request: index %d, value %d\n", index, value);

            snprintf(buffer, sizeof(buffer), "READ index %d value %d\n", index, value);
            send_to_monitor(client_sock, buffer);

            snprintf(buffer, sizeof(buffer), "VALUE %d %d", index, value);
            if (send(client_sock, buffer, strlen(buffer), 0) < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Failed to send VALUE");
                    break;
                }
            }
        } else if (strncmp(buffer, "WRITE", 5) == 0) {
            pthread_mutex_lock(&rw_mutex);
            while (active_writers > 0) {
                printf("Writer waiting: writers %d\n", active_writers);
                pthread_cond_wait(&writer_cond, &rw_mutex);
            }
            active_writers++;
            pthread_mutex_unlock(&rw_mutex);

            int index, new_value;
            if (sscanf(buffer, "WRITE %d %d", &index, &new_value) != 2 || index < 0 || index >= DB_SIZE) {
                printf("Server: Invalid WRITE request: '%s'\n", buffer);
            } else {
                pthread_mutex_lock(&db_mutex);
                int old_value = db[index];
                db[index] = new_value;
                pthread_mutex_unlock(&db_mutex);

                printf("Writer request: index %d, old value %d, new value %d\n", 
                       index, old_value, new_value);

                snprintf(buffer, sizeof(buffer), "WRITE index %d old_value %d new_value %d\n", 
                         index, old_value, new_value);
                send_to_monitor(client_sock, buffer);
                send_db_state(client_sock);
                usleep(30000); // Задержка 30 мс

                snprintf(buffer, sizeof(buffer), "WROTE %d %d %d", index, old_value, new_value);
                if (send(client_sock, buffer, strlen(buffer), 0) < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("Failed to send WROTE");
                        break;
                    }
                }
            }

            pthread_mutex_lock(&rw_mutex);
            active_writers--;
            printf("Writer finished: writers %d\n", active_writers);
            pthread_cond_broadcast(&writer_cond);
            pthread_mutex_unlock(&rw_mutex);
        }
    }

    pthread_mutex_lock(&monitor_mutex);
    if (client_sock == monitor_sock) {
        monitor_sock = -1;
        printf("Monitor disconnected: sock %d\n", client_sock);
    }
    pthread_mutex_unlock(&monitor_mutex);

    close(client_sock);
    free(arg);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    srand(time(NULL));
    for (int i = 0; i < DB_SIZE; i++) {
        db[i] = i + 1;
    }

    int server_fd, *client_sock;
    struct sockaddr_in server_addr, client_addr;
    int addrlen = sizeof(client_addr);
    int port = atoi(argv[2]);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket failed");
        exit(1);
    }

    set_socket_buffers(server_fd);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(1);
    }

    printf("Server listening on %s:%d\n", argv[1], port);

    while (1) {
        client_sock = malloc(sizeof(int));
        *client_sock = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
        if (*client_sock < 0) {
            perror("Accept failed");
            free(client_sock);
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, client_sock) != 0) {
            perror("Thread creation failed");
            close(*client_sock);
            free(client_sock);
        }
        pthread_detach(thread);
    }

    close(server_fd);
    return 0;
}