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

#define MAX_CLIENTS 100
#define BUFFER_SIZE 4096
#define MAX_NICK 32
#define MAX_FILENAME 256

typedef struct {
    int fd;
    char nick[MAX_NICK];
    int state;
    size_t file_size;
    size_t file_received;
    char filename[MAX_FILENAME];
    char buffer[BUFFER_SIZE];
    size_t buf_len;
} client_t;

client_t clients[MAX_CLIENTS];
int listen_fd;
int running = 1;

void sigint_handler(int sig) {
    running = 0;
}

void remove_client(int index) {
    if (clients[index].fd > 0) {
        close(clients[index].fd);
        clients[index].fd = -1;
    }
    memset(&clients[index], 0, sizeof(client_t));
    clients[index].fd = -1;
}

void broadcast_message(const char *msg, size_t len, int sender_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && clients[i].fd != sender_fd) {
            ssize_t sent = send(clients[i].fd, msg, len, 0);
            if (sent == -1) {
                remove_client(i);
            }
        }
    }
}

void process_client(int index) {
    client_t *cl = &clients[index];
    ssize_t n = read(cl->fd, cl->buffer + cl->buf_len, BUFFER_SIZE - cl->buf_len);
    if (n <= 0) {
        if (n == 0) {
            printf("Client %d disconnected\n", index);
        } else {
            perror("read");
        }
        remove_client(index);
        return;
    }
    cl->buf_len += n;

    while (cl->buf_len > 0) {
        if (cl->state == 0) {
            char *newline = memchr(cl->buffer, '\n', cl->buf_len);
            if (!newline) break;
            *newline = '\0';
            char *cmd = cl->buffer;

            if (strncmp(cmd, "NICK ", 5) == 0) {
                strncpy(cl->nick, cmd + 5, MAX_NICK - 1);
                cl->nick[MAX_NICK - 1] = '\0';
            } else if (strncmp(cmd, "MSG ", 4) == 0) {
                char *text = cmd + 4;
                char msg[BUFFER_SIZE];
                int len = snprintf(msg, sizeof(msg), "MSG %s %s\n", cl->nick, text);
                broadcast_message(msg, len, cl->fd);
            } else if (strncmp(cmd, "FILE ", 5) == 0) {
                char filename[MAX_FILENAME];
                size_t size;
                if (sscanf(cmd + 5, "%s %zu", filename, &size) == 2) {
                    strcpy(cl->filename, filename);
                    cl->file_size = size;
                    cl->file_received = 0;
                    cl->state = 1;

                    char header[BUFFER_SIZE];
                    int hlen = snprintf(header, sizeof(header), "FILE %s %s %zu\n", cl->nick, filename, size);
                    broadcast_message(header, hlen, cl->fd);
                }
            }

            size_t consumed = (newline - cl->buffer) + 1;
            if (cl->buf_len > consumed) {
                memmove(cl->buffer, newline + 1, cl->buf_len - consumed);
                cl->buf_len -= consumed;
            } else {
                cl->buf_len = 0;
            }
        } else if (cl->state == 1) {
            size_t to_read = cl->file_size - cl->file_received;
            if (to_read > cl->buf_len) to_read = cl->buf_len;
            if (to_read > 0) {
                broadcast_message(cl->buffer, to_read, cl->fd);
                cl->file_received += to_read;
                memmove(cl->buffer, cl->buffer + to_read, cl->buf_len - to_read);
                cl->buf_len -= to_read;
                if (cl->file_received == cl->file_size) {
                    cl->state = 0;
                    cl->file_size = 0;
                    cl->file_received = 0;
                }
            } else {
                break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int port = 8889;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    signal(SIGINT, sigint_handler);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    struct pollfd pollfds[MAX_CLIENTS + 1];
    int nfds = 1;
    pollfds[0].fd = listen_fd;
    pollfds[0].events = POLLIN;

    while (running) {
        int ret = poll(pollfds, nfds, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        if (pollfds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len);
            if (client_fd < 0) {
                perror("accept");
            } else {
                int idx = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) {
                        idx = i;
                        break;
                    }
                }
                if (idx == -1) {
                    printf("Too many clients, connection refused\n");
                    close(client_fd);
                } else {
                    clients[idx].fd = client_fd;
                    clients[idx].buf_len = 0;
                    clients[idx].state = 0;
                    clients[idx].nick[0] = '\0';
                    pollfds[nfds].fd = client_fd;
                    pollfds[nfds].events = POLLIN;
                    nfds++;
                    printf("New client connected, fd=%d, total clients=%d\n", client_fd, nfds-1);
                }
            }
        }

        for (int i = 0; i < nfds; i++) {
            if (i == 0) continue;
            int fd = pollfds[i].fd;
            if (fd == -1) continue;
            if (pollfds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
                int idx = -1;
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (clients[j].fd == fd) {
                        idx = j;
                        break;
                    }
                }
                if (idx != -1) {
                    process_client(idx);
                    if (clients[idx].fd == -1) {
                        pollfds[i].fd = -1;
                    }
                }
            }
        }

        int new_nfds = 1;
        for (int i = 1; i < nfds; i++) {
            if (pollfds[i].fd != -1) {
                pollfds[new_nfds] = pollfds[i];
                new_nfds++;
            }
        }
        nfds = new_nfds;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1) {
            close(clients[i].fd);
        }
    }
    close(listen_fd);
    return 0;
}
