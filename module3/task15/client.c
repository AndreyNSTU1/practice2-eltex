#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void send_file(int sock, char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) { perror("fopen"); return; }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char cmd[BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "sendfile %s\n", filename);
    send(sock, cmd, strlen(cmd), 0);
    usleep(10000); // небольшая пауза, чтобы сервер перешёл в состояние приёма
    send(sock, &file_size, sizeof(file_size), 0);
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
    char resp[10];
    recv(sock, resp, sizeof(resp), 0);
    if (strcmp(resp, "OK") == 0) printf("Файл передан успешно\n");
    else printf("Ошибка при передаче\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) { fprintf(stderr, "Использование: %s <IP> <порт>\n", argv[0]); exit(1); }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);
    connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    printf("Подключено к %s:%s\n", argv[1], argv[2]);

    char buffer[BUFFER_SIZE];
    while (1) {
        printf("\n> ");
        fflush(stdout);
        if (!fgets(buffer, BUFFER_SIZE, stdin)) break;
        buffer[strcspn(buffer, "\n")] = 0;
        if (strcmp(buffer, "exit") == 0) break;
        if (strncmp(buffer, "sendfile", 8) == 0) {
            char filename[256];
            sscanf(buffer, "sendfile %s", filename);
            send_file(sock, filename);
        } else {
            strcat(buffer, "\n");
            send(sock, buffer, strlen(buffer), 0);
            memset(buffer, 0, BUFFER_SIZE);
            recv(sock, buffer, BUFFER_SIZE, 0);
            printf("%s\n", buffer);
        }
    }
    close(sock);
    return 0;
}
