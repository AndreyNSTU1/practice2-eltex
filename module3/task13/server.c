#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Обработка арифметических команд
void handle_arithmetic(char *cmd, char *response) {
    char op[10];
    double a, b;
    if (sscanf(cmd, "%s %lf %lf", op, &a, &b) != 3) {
        sprintf(response, "Ошибка: неверный формат команды");
        return;
    }
    if (strcmp(op, "diff") == 0) {
        sprintf(response, "Разность: %.2lf", a - b);
    } else if (strcmp(op, "prod") == 0) {
        sprintf(response, "Произведение: %.2lf", a * b);
    } else if (strcmp(op, "quot") == 0) {
        if (b == 0) sprintf(response, "Ошибка: деление на ноль");
        else sprintf(response, "Частное: %.2lf", a / b);
    } else {
        sprintf(response, "Неизвестная операция");
    }
}

// Приём файла от клиента
int receive_file(int client_socket, char *filename) {
    char buffer[BUFFER_SIZE];
    // Сначала читаем размер файла (8 байт)
    long file_size;
    if (recv(client_socket, &file_size, sizeof(file_size), 0) != sizeof(file_size)) {
        return -1;
    }
    // Создаём файл для записи
    char fullpath[256];
    snprintf(fullpath, sizeof(fullpath), "received_%s", filename);
    FILE *f = fopen(fullpath, "wb");
    if (!f) return -1;

    long received = 0;
    while (received < file_size) {
        int need = (file_size - received) < BUFFER_SIZE ? (file_size - received) : BUFFER_SIZE;
        int n = recv(client_socket, buffer, need, 0);
        if (n <= 0) break;
        fwrite(buffer, 1, n, f);
        received += n;
    }
    fclose(f);
    return (received == file_size) ? 0 : -1;
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Создание сокета
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Привязка к порту
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Ожидание соединений
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Сервер запущен на порту %d\n", PORT);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }
        printf("Клиент подключен: %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));

        char buffer[BUFFER_SIZE] = {0};
        while (1) {
            int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_read <= 0) {
                printf("Клиент отключился\n");
                break;
            }
            buffer[bytes_read] = '\0';

            // Отделяем команду
            if (strncmp(buffer, "sendfile", 8) == 0) {
                char filename[256];
                sscanf(buffer, "sendfile %s", filename);
                printf("Приём файла: %s\n", filename);
                int res = receive_file(client_socket, filename);
                if (res == 0)
                    send(client_socket, "OK", 2, 0);
                else
                    send(client_socket, "ERR", 3, 0);
            }
            else {
                // Арифметическая команда
                char response[BUFFER_SIZE];
                handle_arithmetic(buffer, response);
                send(client_socket, response, strlen(response), 0);
            }
        }
        close(client_socket);
    }
    close(server_fd);
    return 0;
}
