#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

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

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl failed");
        close(sock);
        return -1;
    }

    return sock;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE] = {0};
    int message_len = 0;

    while (1) {
        int sock = connect_to_server(ip, port);
        if (sock < 0) {
            printf("Monitor: Reconnecting in 2 seconds...\n");
            sleep(2);
            continue;
        }

        printf("Monitor: Connected to server\n");

        snprintf(buffer, sizeof(buffer), "MONITOR");
        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            perror("Failed to send MONITOR");
            close(sock);
            sleep(2);
            continue;
        }

        while (1) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);
            struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

            int ready = select(sock + 1, &read_fds, NULL, NULL, &tv);
            if (ready < 0) {
                perror("Select error");
                close(sock);
                break;
            }
            if (ready == 0) {
                continue;
            }

            int valread = read(sock, buffer, sizeof(buffer) - 1);
            if (valread <= 0) {
                if (valread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Read error");
                } else {
                    printf("Monitor: Server disconnected\n");
                }
                close(sock);
                break;
            }

            buffer[valread] = '\0';
            if (message_len + valread >= sizeof(message)) {
                printf("Monitor: Buffer overflow, resetting\n");
                message_len = 0;
                message[0] = '\0';
                continue;
            }
            memcpy(message + message_len, buffer, valread);
            message_len += valread;

            char *line = message;
            char *line_end;
            while ((line_end = strchr(line, '\n'))) {
                int len = line_end - line;
                if (len >= BUFFER_SIZE) {
                    printf("Monitor: Line too long, skipping\n");
                    line = line_end + 1;
                    continue;
                }

                char msg[BUFFER_SIZE];
                strncpy(msg, line, len);
                msg[len] = '\0';
                printf("Monitor: Received: '%s'\n", msg);

                if (strncmp(msg, "READ", 4) == 0) {
                    int index, value;
                    if (sscanf(msg, "READ index %d value %d", &index, &value) == 2) {
                        printf("Monitor: Reader: Index %d, Value %d\n", index, value);
                    } else {
                        printf("Monitor: Failed to parse READ: '%s'\n", msg);
                    }
                } else if (strncmp(msg, "WRITE", 5) == 0) {
                    int index, old_value, new_value;
                    if (sscanf(msg, "WRITE index %d old_value %d new_value %d", 
                               &index, &old_value, &new_value) == 3) {
                        printf("Monitor: Writer: Index %d, Old Value %d, New Value %d\n", 
                               index, old_value, new_value);
                    } else {
                        printf("Monitor: Failed to parse WRITE: '%s'\n", msg);
                    }
                } else if (strncmp(msg, "DB", 2) == 0) {
                    printf("Monitor: Database: %s\n", msg + 3);
                } else {
                    printf("Monitor: Unknown message: '%s'\n", msg);
                }

                line = line_end + 1;
            }

            if (line < message + message_len) {
                memmove(message, line, message + message_len - line);
                message_len = message + message_len - line;
            } else {
                message_len = 0;
            }
            message[message_len] = '\0';
        }

        close(sock);
        printf("Monitor: Reconnecting in 2 seconds...\n");
        sleep(2);
    }

    return 0;
}