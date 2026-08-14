#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define PORT        8888
#define BUFSIZE     1024
#define MAX_NICK    32

int running = 1;
int sock = -1;
pid_t my_pid;
char my_nick[MAX_NICK];

void sigint_handler(int sig) {
    if (running) {
        running = 0;
        char leave_msg[BUFSIZE];
        snprintf(leave_msg, sizeof(leave_msg), "LEAVE:%d:%s:", my_pid, my_nick);
        struct sockaddr_in broadcast_addr;
        memset(&broadcast_addr, 0, sizeof(broadcast_addr));
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_port = htons(PORT);
        broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
        sendto(sock, leave_msg, strlen(leave_msg), 0,
               (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
        printf("Покидаем чат...\n");
        close(sock);
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <nickname>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    strncpy(my_nick, argv[1], MAX_NICK - 1);
    my_nick[MAX_NICK - 1] = '\0';
    my_pid = getpid();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(sock);
        exit(EXIT_FAILURE);
    }
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("setsockopt SO_BROADCAST");
        close(sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, sigint_handler);

    char join_msg[BUFSIZE];
    snprintf(join_msg, sizeof(join_msg), "JOIN:%d:%s:", my_pid, my_nick);
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(sock, join_msg, strlen(join_msg), 0,
           (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    printf("Вы вошли в чат как %s (PID %d)\n", my_nick, my_pid);

    fd_set readfds;
    struct timeval tv;
    char buffer[BUFSIZE];
    int n;

    while (running) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(sock + 1, &readfds, NULL, NULL, &tv) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(sock, &readfds)) {
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            n = recvfrom(sock, buffer, BUFSIZE - 1, 0,
                         (struct sockaddr*)&from, &from_len);
            if (n <= 0) continue;
            buffer[n] = '\0';

            char cmd[16], nick[MAX_NICK], text[BUFSIZE];
            pid_t sender_pid;
            if (sscanf(buffer, "%15[^:]:%d:%31[^:]:%[^\n]", cmd, &sender_pid, nick, text) == 4) {
                if (sender_pid == my_pid) continue;

                if (strcmp(cmd, "JOIN") == 0) {
                    printf("[Система] %s (PID %d) присоединился\n", nick, sender_pid);
                } else if (strcmp(cmd, "LEAVE") == 0) {
                    printf("[Система] %s (PID %d) покинул чат\n", nick, sender_pid);
                } else if (strcmp(cmd, "MSG") == 0) {
                    printf("%s: %s\n", nick, text);
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, BUFSIZE, stdin) == NULL) {
                running = 0;
                break;
            }
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n')
                buffer[len - 1] = '\0';
            if (strlen(buffer) == 0) continue;

            char msg[BUFSIZE];
            snprintf(msg, sizeof(msg), "MSG:%d:%s:%s", my_pid, my_nick, buffer);
            sendto(sock, msg, strlen(msg), 0,
                   (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
        }
    }

    close(sock);
    return 0;
}
