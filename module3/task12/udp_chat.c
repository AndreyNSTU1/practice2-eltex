#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024

int sock = -1;
struct sockaddr_in peer_addr;
volatile int running = 1;

void *receive_thread(void *arg) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);

    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t n = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0,
                             (struct sockaddr*)&from_addr, &addr_len);
        if (n > 0) {
            buffer[n] = '\0';
            printf("\r\033[K[Собеседник]: %s\n", buffer);
            printf("Вы: ");
            fflush(stdout);
        } else if (n == -1) {
            break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Использование: %s <мой_порт> <IP_собеседника> <порт_собеседника>\n", argv[0]);
        fprintf(stderr, "Пример: %s 5000 127.0.0.1 5001\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int my_port = atoi(argv[1]);
    char *peer_ip = argv[2];
    int peer_port = atoi(argv[3]);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(my_port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
        perror("bind"); close(sock); exit(EXIT_FAILURE);
    }

    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(peer_port);
    if (inet_pton(AF_INET, peer_ip, &peer_addr.sin_addr) <= 0) {
        perror("inet_pton"); close(sock); exit(EXIT_FAILURE);
    }

    printf("Чат запущен. Ваш порт: %d, собеседник: %s:%d\n", my_port, peer_ip, peer_port);
    printf("Введите сообщение (или 'exit' для выхода):\n");

    pthread_t recv_thread_id;
    if (pthread_create(&recv_thread_id, NULL, receive_thread, NULL) != 0) {
        perror("pthread_create"); close(sock); exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    while (running) {
        printf("Вы: ");
        fflush(stdout);
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "exit") == 0) {
            printf("Завершение чата.\n");
            running = 0;
            break;
        }

        if (sendto(sock, buffer, strlen(buffer), 0,
                   (struct sockaddr*)&peer_addr, sizeof(peer_addr)) == -1)
            perror("sendto");
    }

    shutdown(sock, SHUT_RD);
    pthread_join(recv_thread_id, NULL);
    close(sock);
    return 0;
}
