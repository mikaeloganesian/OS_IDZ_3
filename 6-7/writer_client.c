#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>

#define DB_SIZE 10
#define BUFFER_SIZE 256

int connect_to_server(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation error");
        return -1;
    }

    int buf_size = 65536;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));

    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(port) };
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    return sock;
}

void writer_process(int writer_id, const char *ip, int port) {
    char buffer[BUFFER_SIZE];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned)(ts.tv_nsec ^ getpid()));

    int recent_indices[3] = {-1, -1, -1};
    int recent_count = 0;

    while (1) {
        int sock = connect_to_server(ip, port);
        if (sock < 0) {
            printf("Writer %d (PID %d): Reconnecting in 2 seconds...\n", writer_id, getpid());
            sleep(2);
            continue;
        }

        printf("Writer %d (PID %d): Connected to server\n", writer_id, getpid());

        while (1) {
            usleep(rand() % 500000); // Задержка 0-500 мс
            int index;
            do {
                index = rand() % DB_SIZE;
                int used_recently = 0;
                for (int i = 0; i < recent_count; i++) {
                    if (recent_indices[i] == index) {
                        used_recently = 1;
                        break;
                    }
                }
                if (!used_recently) break;
            } while (1);

            if (recent_count < 3) {
                recent_indices[recent_count++] = index;
            } else {
                for (int i = 1; i < 3; i++) {
                    recent_indices[i - 1] = recent_indices[i];
                }
                recent_indices[2] = index;
            }

            int new_value = rand() % 100 + 1;
            printf("Writer %d (PID %d): Generated index %d, new value %d\n", 
                   writer_id, getpid(), index, new_value);
            snprintf(buffer, sizeof(buffer), "WRITE %d %d", index, new_value);
            if (send(sock, buffer, strlen(buffer), 0) < 0) {
                perror("Send failed");
                break;
            }

            int valread = read(sock, buffer, sizeof(buffer) - 1);
            if (valread <= 0) {
                if (valread < 0) perror("Read error");
                else printf("Writer %d (PID %d): Server disconnected\n", writer_id, getpid());
                break;
            }
            buffer[valread] = '\0';

            int old_value;
            if (sscanf(buffer, "WROTE %d %d %d", &index, &old_value, &new_value) != 3) {
                printf("Writer %d (PID %d): Failed to parse WROTE: '%s'\n", writer_id, getpid(), buffer);
                continue;
            }
            printf("Writer %d (PID %d): Index %d, Old Value %d, New Value %d\n", 
                   writer_id, getpid(), index, old_value, new_value);

            sleep(2);
        }

        close(sock);
        printf("Writer %d (PID %d): Reconnecting in 2 seconds...\n", writer_id, getpid());
        sleep(2);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IP> <port> <num_writers>\n", argv[0]);
        exit(1);
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int num_writers = atoi(argv[3]);

    for (int i = 0; i < num_writers; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            writer_process(i + 1, ip, port);
            exit(0);
        } else if (pid < 0) {
            perror("Fork failed");
        }
    }

    for (int i = 0; i < num_writers; i++) {
        wait(NULL);
    }

    return 0;
}