#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_READERS 3

// Функция вычисления числа Фибоначчи
long long fibonacci(int n) {
    if (n <= 1) return n;
    long long a = 0, b = 1, c;
    for (int i = 2; i <= n; i++) {
        c = a + b;
        a = b;
        b = c;
    }
    return b;
}

void reader_process(int reader_id, char *ip, int port) {
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
        // Отправка запроса на чтение
        strcpy(buffer, "READ");
        send(sock, buffer, strlen(buffer), 0);

        // Получение ответа
        int valread = read(sock, buffer, 256);
        if (valread <= 0) break;
        buffer[valread] = '\0';

        int index, value;
        sscanf(buffer, "VALUE %d %d", &index, &value);
        long long fib = fibonacci(value % 20); // Ограничение для чисел Фибоначчи
        printf("Reader %d (PID %d): Index %d, Value %d, Fibonacci %lld\n", reader_id, getpid(), index, value, fib);

        sleep(2); // Задержка для имитации периодичности
    }

    close(sock);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IP> <port> <num_readers>\n", argv[0]);
        exit(1);
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    int num_readers = atoi(argv[3]);

    for (int i = 0; i < num_readers; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            reader_process(i + 1, ip, port);
            exit(0);
        } else if (pid < 0) {
            perror("Fork failed");
        }
    }

    // Ожидание завершения процессов
    for (int i = 0; i < num_readers; i++) {
        wait(NULL);
    }

    return 0;
}