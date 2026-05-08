#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8080
#define MAX_EVENTS 10
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

// Состояние клиента
typedef enum {
    STATE_IDLE,
    STATE_RECV_FILE_SIZE,
    STATE_RECV_FILE_DATA
} client_state;

typedef struct {
    int fd;
    client_state state;
    char filename[256];
    long file_size;
    long received_bytes;
    FILE *output_file;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
} client_info;

client_info clients[MAX_CLIENTS];

// Установка неблокирующего режима
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Отправить ответ клиенту
void send_response(int fd, const char *msg) {
    send(fd, msg, strlen(msg), 0);
}

// Обработка арифметики
void handle_arithmetic(const char *cmd, char *response) {
    char op[10];
    double a, b;
    if (sscanf(cmd, "%s %lf %lf", op, &a, &b) != 3) {
        sprintf(response, "Ошибка: неверный формат (нужно: diff a b)");
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
        sprintf(response, "Неизвестная операция (diff, prod, quot)");
    }
}

// Начать приём файла
void start_file_receive(client_info *c, const char *filename) {
    strcpy(c->filename, filename);
    c->state = STATE_RECV_FILE_SIZE;
    c->received_bytes = 0;
    c->file_size = 0;
    // ответ не шлём, ждём размер
}

// Обработка данных от клиента (неблокирующая)
void process_client(client_info *c) {
    char *ptr = c->buffer;
    size_t remaining = c->buffer_len;
    char line[BUFFER_SIZE];
    int line_index = 0;

    while (remaining > 0) {
        if (c->state == STATE_IDLE) {
            // Читаем до \n
            while (line_index < remaining && ptr[line_index] != '\n') line_index++;
            if (line_index < remaining && ptr[line_index] == '\n') {
                // нашли конец команды
                memcpy(line, ptr, line_index);
                line[line_index] = '\0';
                // сдвигаем буфер
                remaining -= (line_index + 1);
                ptr += (line_index + 1);
                line_index = 0;

                // Обрабатываем команду
                if (strncmp(line, "sendfile", 8) == 0) {
                    char filename[256];
                    sscanf(line, "sendfile %s", filename);
                    start_file_receive(c, filename);
                    // ответим позже, когда файл примем
                    // Если команда "sendfile" без аргументов? Игнорируем
                } else if (strcmp(line, "exit") == 0) {
                    // клиент хочет выйти
                    c->state = STATE_IDLE;  // закроем сокет позже
                    return;
                } else {
                    char response[BUFFER_SIZE];
                    handle_arithmetic(line, response);
                    send_response(c->fd, response);
                }
            } else {
                // неполная команда, сохраняем остаток
                if (ptr != c->buffer) {
                    memmove(c->buffer, ptr, remaining);
                }
                c->buffer_len = remaining;
                return;
            }
        }
        else if (c->state == STATE_RECV_FILE_SIZE) {
            // ожидаем 8 байт размера файла
            long need = 8 - c->received_bytes;
            if (remaining >= need) {
                memcpy(((char*)&c->file_size) + c->received_bytes, ptr, need);
                c->received_bytes = 0;
                remaining -= need;
                ptr += need;

                // переходим к приёму данных
                char fullpath[256];
                snprintf(fullpath, sizeof(fullpath), "received_%s", c->filename);
                c->output_file = fopen(fullpath, "wb");
                if (!c->output_file) {
                    send_response(c->fd, "ERR");
                    c->state = STATE_IDLE;
                } else {
                    c->state = STATE_RECV_FILE_DATA;
                    c->received_bytes = 0;
                }
            } else {
                memcpy(((char*)&c->file_size) + c->received_bytes, ptr, remaining);
                c->received_bytes += remaining;
                c->buffer_len = 0;
                return;
            }
        }
        else if (c->state == STATE_RECV_FILE_DATA) {
            long need = c->file_size - c->received_bytes;
            if (need <= 0) {
                // файл закончился, закрываем
                fclose(c->output_file);
                send_response(c->fd, "OK");
                c->state = STATE_IDLE;
                continue; // продолжим обработку остатка буфера (если есть следующая команда)
            }
            size_t to_write = (remaining < need) ? remaining : need;
            if (to_write > 0) {
                fwrite(ptr, 1, to_write, c->output_file);
                c->received_bytes += to_write;
                remaining -= to_write;
                ptr += to_write;
            }
            if (c->received_bytes >= c->file_size) {
                fclose(c->output_file);
                send_response(c->fd, "OK");
                c->state = STATE_IDLE;
            }
            if (remaining == 0) break;
        }
    }
    // сохраняем остаток буфера
    if (ptr != c->buffer && remaining > 0) {
        memmove(c->buffer, ptr, remaining);
    }
    c->buffer_len = remaining;
}

int main() {
    int server_fd;
    struct sockaddr_in addr;
    int epoll_fd;
    struct epoll_event ev, events[MAX_EVENTS];

    // Создание серверного сокета
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) perror("socket"), exit(1);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) perror("bind"), exit(1);
    if (listen(server_fd, 10) < 0) perror("listen"), exit(1);
    set_nonblocking(server_fd);

    // Создание epoll
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) perror("epoll_create1"), exit(1);
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) perror("epoll_ctl"), exit(1);

    // Инициализация клиентов
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    printf("Сервер с epoll запущен на порту %d\n", PORT);

    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) perror("epoll_wait"), exit(1);

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                // Новое подключение
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) continue;
                set_nonblocking(client_fd);

                // Находим свободный слот
                int idx;
                for (idx = 0; idx < MAX_CLIENTS; idx++) {
                    if (clients[idx].fd == -1) break;
                }
                if (idx >= MAX_CLIENTS) {
                    close(client_fd);
                    continue;
                }
                clients[idx].fd = client_fd;
                clients[idx].state = STATE_IDLE;
                clients[idx].buffer_len = 0;
                clients[idx].received_bytes = 0;
                clients[idx].output_file = NULL;

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                printf("Клиент подключён: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            }
            else {
                // Данные от клиента
                client_info *c = NULL;
                int idx;
                for (idx = 0; idx < MAX_CLIENTS; idx++) {
                    if (clients[idx].fd == fd) {
                        c = &clients[idx];
                        break;
                    }
                }
                if (!c) continue;

                // Читаем данные (неблокирующий режим)
                ssize_t n = recv(fd, c->buffer + c->buffer_len, sizeof(c->buffer) - c->buffer_len - 1, 0);
                if (n <= 0) {
                    // клиент отключился
                    if (c->output_file) fclose(c->output_file);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    c->fd = -1;
                    printf("Клиент отключился\n");
                } else {
                    c->buffer_len += n;
                    c->buffer[c->buffer_len] = '\0';
                    process_client(c);
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}
