#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define MAX_DB_SIZE 100
#define MAX_CLIENTS 50

int db[MAX_DB_SIZE];
int db_size = 10; 
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
int active_writers = 0;
int active_readers = 0;
pthread_mutex_t rw_mutex = PTHREAD_MUTEX_INITIALIZER;

void sort_db() {
    for (int i = 0; i < db_size - 1; i++) {
        for (int j = 0; j < db_size - i - 1; j++) {
            if (db[j] > db[j + 1]) {
                int temp = db[j];
                db[j] = db[j + 1];
                db[j + 1] = temp;
            }
        }
    }
}

void *handle_client(void *arg) {
    int client_sock = *(int *)arg;
    char buffer[256];
    int valread;

    while ((valread = read(client_sock, buffer, 256)) > 0) {
        buffer[valread] = '\0';
        if (strncmp(buffer, "READ", 4) == 0) {
            pthread_mutex_lock(&rw_mutex);
            active_readers++;
            pthread_mutex_unlock(&rw_mutex);

            pthread_mutex_lock(&db_mutex);
            int index = rand() % db_size;
            int value = db[index];
            pthread_mutex_unlock(&db_mutex);

            sprintf(buffer, "VALUE %d %d", index, value);
            send(client_sock, buffer, strlen(buffer), 0);

            pthread_mutex_lock(&rw_mutex);
            active_readers--;
            pthread_mutex_unlock(&rw_mutex);
        } else if (strncmp(buffer, "WRITE", 5) == 0) {
            pthread_mutex_lock(&rw_mutex);
            while (active_readers > 0 || active_writers > 0) {
                pthread_mutex_unlock(&rw_mutex);
                usleep(1000);
                pthread_mutex_lock(&rw_mutex);
            }
            active_writers++;
            pthread_mutex_unlock(&rw_mutex);

            pthread_mutex_lock(&db_mutex);
            int index, old_value, new_value;
            sscanf(buffer + 6, "%d %d", &index, &new_value);
            old_value = db[index];
            db[index] = new_value;
            sort_db();
            pthread_mutex_unlock(&db_mutex);

            sprintf(buffer, "WROTE %d %d %d", index, old_value, new_value);
            send(client_sock, buffer, strlen(buffer), 0);

            pthread_mutex_lock(&rw_mutex);
            active_writers--;
            pthread_mutex_unlock(&rw_mutex);
        }
    }

    close(client_sock);
    free(arg);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    for (int i = 0; i < db_size; i++) {
        db[i] = i + 1; 
    }

    int server_fd, new_socket, *client_sock;
    struct sockaddr_in server_addr, client_addr;
    int addrlen = sizeof(client_addr);
    int port = atoi(argv[2]);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(1);
    }

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
        if ((*client_sock = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen)) < 0) {
            perror("Accept failed");
            free(client_sock);
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, (void *)client_sock) != 0) {
            perror("Thread creation failed");
            close(*client_sock);
            free(client_sock);
        }
        pthread_detach(thread);
    }

    close(server_fd);
    return 0;
}