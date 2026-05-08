#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Отправка файла на сервер
int send_file(int sock, char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }
    // Определяем размер файла
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    // Отправляем команду sendfile и имя
    char cmd[BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "sendfile %s", filename);
    send(sock, cmd, strlen(cmd), 0);
    // Ждём подтверждения? Упрощённо – сразу шлём размер
    // Но лучше дождаться готовности сервера. Сервер читает размер сразу.
    // Отправляем размер файла
    send(sock, &file_size, sizeof(file_size), 0);
    // Отправляем содержимое
    char buffer[BUFFER_SIZE];
    long sent = 0;
    while (sent < file_size) {
        int need = (file_size - sent) < BUFFER_SIZE ? (file_size - sent) : BUFFER_SIZE;
        size_t n = fread(buffer, 1, need, f);
        if (n <= 0) break;
        send(sock, buffer, n, 0);
        sent += n;
    }
    fclose(f);
    // Принимаем ответ сервера
    char resp[10];
    recv(sock, resp, sizeof(resp), 0);
    if (strcmp(resp, "OK") == 0) {
        printf("Файл %s успешно передан\n", filename);
        return 0;
    } else {
        printf("Ошибка приёма файла на сервере\n");
        return -1;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Использование: %s <IP сервера> <порт>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    printf("Подключено к серверу %s:%s\n", argv[1], argv[2]);

    while (1) {
        printf("\nВведите команду:\n");
        printf("  diff a b  - разность\n");
        printf("  prod a b  - произведение\n");
        printf("  quot a b  - частное\n");
        printf("  sendfile <имя_файла> - передать файл\n");
        printf("  exit      - выход\n");
        printf("> ");
        fflush(stdout);

        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "exit") == 0) {
            break;
        }

        if (strncmp(buffer, "sendfile", 8) == 0) {
            char filename[256];
            sscanf(buffer, "sendfile %s", filename);
            send_file(sock, filename);
        } else {
            send(sock, buffer, strlen(buffer), 0);
            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
            if (bytes > 0) {
                printf("Ответ сервера: %s\n", buffer);
            } else {
                printf("Сервер разорвал соединение\n");
                break;
            }
        }
    }
    close(sock);
    return 0;
}
