#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_WRITERS 2

void writer_process(int writer_id, char *ip, int port) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[256] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }

    while (1) {
        int index = rand() % 10; 
        int new_value = rand() % 100 + 1;
        sprintf(buffer, "WRITE %d %d", index, new_value);
        send(sock, buffer, strlen(buffer), 0);

        int valread = read(sock, buffer, 256);
        if (valread <= 0) break;
        buffer[valread] = '\0';

        int old_value;
        sscanf(buffer, "WROTE %d %d %d", &index, &old_value, &new_value);
        printf("Writer %d (PID %d): Index %d, Old Value %d, New Value %d\n", writer_id, getpid(), index, old_value, new_value);

        sleep(3); 
    }

    close(sock);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IP> <port> <num_writers>\n", argv[0]);
        exit(1);
    }

    char *ip = argv[1];
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