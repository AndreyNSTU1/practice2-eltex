#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096
#define MAX_NICK 32
#define MAX_FILENAME 256

int sock_fd;
int running = 1;
char my_nick[MAX_NICK];

void sigint_handler(int sig) {
    running = 0;
    if (sock_fd > 0) {
        close(sock_fd);
    }
    exit(0);
}

int send_file(int sock, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return -1;
    }
    char cmd[BUFFER_SIZE];
    int len = snprintf(cmd, sizeof(cmd), "FILE %s %ld\n", filename, size);
    if (send(sock, cmd, len, 0) < 0) {
        perror("send file cmd");
        fclose(f);
        return -1;
    }
    char buffer[BUFFER_SIZE];
    size_t total = 0;
    while (total < size) {
        size_t to_read = BUFFER_SIZE;
        if (to_read > (size - total)) to_read = size - total;
        size_t n = fread(buffer, 1, to_read, f);
        if (n <= 0) break;
        if (send(sock, buffer, n, 0) < 0) {
            perror("send file data");
            fclose(f);
            return -1;
        }
        total += n;
    }
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Использование: %s <server_ip> <port> <nick>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    strncpy(my_nick, argv[3], MAX_NICK - 1);
    my_nick[MAX_NICK - 1] = '\0';

    signal(SIGINT, sigint_handler);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    char nick_msg[BUFFER_SIZE];
    int nlen = snprintf(nick_msg, sizeof(nick_msg), "NICK %s\n", my_nick);
    if (send(sock_fd, nick_msg, nlen, 0) < 0) {
        perror("send nick");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Подключено к серверу %s:%d. Ник: %s\n", server_ip, port, my_nick);
    printf("Введите сообщение или /sendfile <filename>\n");

    struct pollfd fds[2];
    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;

    char buffer[BUFFER_SIZE];
    size_t buf_len = 0;

    while (running) {
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            ssize_t n = read(sock_fd, buffer + buf_len, BUFFER_SIZE - buf_len);
            if (n <= 0) {
                if (n == 0) {
                    printf("Сервер закрыл соединение\n");
                } else {
                    perror("read");
                }
                running = 0;
                break;
            }
            buf_len += n;

            while (buf_len > 0) {
                char *newline = memchr(buffer, '\n', buf_len);
                if (!newline) break;
                *newline = '\0';
                char *cmd = buffer;

                if (strncmp(cmd, "MSG ", 4) == 0) {
                    char nick[MAX_NICK], text[BUFFER_SIZE];
                    if (sscanf(cmd + 4, "%s %[^\n]", nick, text) == 2) {
                        printf("%s: %s\n", nick, text);
                    }
                } else if (strncmp(cmd, "FILE ", 5) == 0) {
                    char nick[MAX_NICK], filename[MAX_FILENAME];
                    size_t size;
                    if (sscanf(cmd + 5, "%s %s %zu", nick, filename, &size) == 3) {
                        printf("Получение файла %s от %s (размер %zu байт)\n", filename, nick, size);
                        char outname[512];
                        snprintf(outname, sizeof(outname), "received_%s_%s", nick, filename);
                        FILE *f = fopen(outname, "wb");
                        if (!f) {
                            perror("fopen for receive");
                        } else {
                            size_t received = 0;
                            size_t consumed = (newline - buffer) + 1;
                            size_t data_in_buf = buf_len - consumed;
                            if (data_in_buf > 0) {
                                size_t to_write = data_in_buf;
                                if (to_write > size - received) to_write = size - received;
                                fwrite(buffer + consumed, 1, to_write, f);
                                received += to_write;
                                memmove(buffer, buffer + consumed + to_write, buf_len - consumed - to_write);
                                buf_len -= (consumed + to_write);
                            } else {
                                buf_len = 0;
                            }
                            while (received < size) {
                                ssize_t nread = read(sock_fd, buffer, BUFFER_SIZE);
                                if (nread <= 0) {
                                    printf("Ошибка при получении файла\n");
                                    break;
                                }
                                size_t to_write = nread;
                                if (to_write > size - received) to_write = size - received;
                                fwrite(buffer, 1, to_write, f);
                                received += to_write;
                            }
                            fclose(f);
                            printf("Файл сохранен как %s\n", outname);
                        }
                    }
                } else {
                    printf("Неизвестная команда: %s\n", cmd);
                }

                if (buf_len > 0) {
                    size_t consumed = (newline - buffer) + 1;
                    if (buf_len > consumed) {
                        memmove(buffer, newline + 1, buf_len - consumed);
                        buf_len -= consumed;
                    } else {
                        buf_len = 0;
                    }
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            char input[BUFFER_SIZE];
            if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
                running = 0;
                break;
            }
            size_t len = strlen(input);
            if (len > 0 && input[len-1] == '\n') input[len-1] = '\0';

            if (strlen(input) == 0) continue;

            if (strncmp(input, "/sendfile ", 10) == 0) {
                char *filename = input + 10;
                send_file(sock_fd, filename);
            } else {
                char msg[BUFFER_SIZE];
                int mlen = snprintf(msg, sizeof(msg), "MSG %s\n", input);
                if (send(sock_fd, msg, mlen, 0) < 0) {
                    perror("send");
                    running = 0;
                    break;
                }
            }
        }
    }

    close(sock_fd);
    return 0;
}
