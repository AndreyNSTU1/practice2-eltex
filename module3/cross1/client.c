// client.c
// Echo-client on raw sockets (UDP) with configurable client port
// Compile: gcc -Wall -Wextra -o client client.c
// Usage:   ./client <server_ip> [client_port]
// Run:     sudo ./client 127.0.0.1 [9999]

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>

#define SERVER_PORT 8888
#define DEFAULT_CLIENT_PORT 9999
#define BUFFER_SIZE 65536

int raw_sock = -1;
volatile sig_atomic_t running = 1;
struct sockaddr_in server_addr;
uint16_t client_port = DEFAULT_CLIENT_PORT;

uint16_t checksum(void *data, int len) {
    uint32_t sum = 0;
    uint16_t *ptr = data;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1)
        sum += *(unsigned char *)ptr;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)~sum;
}

void send_packet(const char *message, int msg_len) {
    struct iphdr ip_hdr;
    struct udphdr udp_hdr;

    memset(&ip_hdr, 0, sizeof(ip_hdr));
    ip_hdr.version = 4;
    ip_hdr.ihl = 5;
    ip_hdr.tot_len = htons(sizeof(ip_hdr) + sizeof(udp_hdr) + msg_len);
    ip_hdr.id = htons(rand() & 0xFFFF);
    ip_hdr.ttl = 64;
    ip_hdr.protocol = IPPROTO_UDP;
    ip_hdr.saddr = htonl(INADDR_ANY);
    ip_hdr.daddr = server_addr.sin_addr.s_addr;
    ip_hdr.check = checksum(&ip_hdr, sizeof(ip_hdr));

    udp_hdr.source = htons(client_port);
    udp_hdr.dest = htons(SERVER_PORT);
    udp_hdr.len = htons(sizeof(udp_hdr) + msg_len);
    udp_hdr.check = 0;

    struct pseudo_header {
        uint32_t src_addr;
        uint32_t dst_addr;
        uint8_t zero;
        uint8_t protocol;
        uint16_t udp_len;
    } pseudo;
    pseudo.src_addr = ip_hdr.saddr;
    pseudo.dst_addr = ip_hdr.daddr;
    pseudo.zero = 0;
    pseudo.protocol = IPPROTO_UDP;
    pseudo.udp_len = udp_hdr.len;

    char udp_buf[sizeof(pseudo) + sizeof(udp_hdr) + msg_len];
    memcpy(udp_buf, &pseudo, sizeof(pseudo));
    memcpy(udp_buf + sizeof(pseudo), &udp_hdr, sizeof(udp_hdr));
    memcpy(udp_buf + sizeof(pseudo) + sizeof(udp_hdr), message, msg_len);
    udp_hdr.check = checksum(udp_buf, sizeof(pseudo) + sizeof(udp_hdr) + msg_len);

    char packet[BUFFER_SIZE];
    memcpy(packet, &ip_hdr, sizeof(ip_hdr));
    memcpy(packet + sizeof(ip_hdr), &udp_hdr, sizeof(udp_hdr));
    memcpy(packet + sizeof(ip_hdr) + sizeof(udp_hdr), message, msg_len);

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_addr = server_addr.sin_addr;
    dest.sin_port = 0;

    int sent = sendto(raw_sock, packet, ntohs(ip_hdr.tot_len), 0,
                      (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0) {
        perror("sendto");
    } else {
        printf("Sent: \"%s\"\n", message);
    }
}

void send_close() {
    printf("Sending CLOSE to server...\n");
    send_packet("CLOSE", 5);
}

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
    }
}

void cleanup() {
    if (raw_sock >= 0)
        close(raw_sock);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip> [client_port]\n", argv[0]);
        return 1;
    }

    if (argc >= 3) {
        client_port = atoi(argv[2]);
        if (client_port == 0) {
            fprintf(stderr, "Invalid port, using default %d\n", DEFAULT_CLIENT_PORT);
            client_port = DEFAULT_CLIENT_PORT;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (raw_sock < 0) {
        perror("socket");
        fprintf(stderr, "Need root privileges.\n");
        return 1;
    }

    int one = 1;
    if (setsockopt(raw_sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt IP_HDRINCL");
        cleanup();
        return 1;
    }

    printf("Client started. Sending from port %u to server %s:%d\n",
           client_port, argv[1], SERVER_PORT);
    printf("Type messages and press Enter. Ctrl+C to quit.\n");

    char input[BUFFER_SIZE];
    fd_set fds;
    int max_fd = (raw_sock > STDIN_FILENO) ? raw_sock : STDIN_FILENO;

    while (running) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(raw_sock, &fds);

        if (select(max_fd + 1, &fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (fgets(input, sizeof(input), stdin) == NULL) {
                break;
            }
            int len = strlen(input);
            if (len > 0 && input[len-1] == '\n')
                input[len-1] = '\0';
            if (len <= 1) continue;
            send_packet(input, strlen(input));
        }

        if (FD_ISSET(raw_sock, &fds)) {
            char buffer[BUFFER_SIZE];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            int recv_len = recvfrom(raw_sock, buffer, BUFFER_SIZE, 0,
                                    (struct sockaddr *)&from, &from_len);
            if (recv_len < 0) {
                if (errno == EINTR) continue;
                perror("recvfrom");
                break;
            }

            struct iphdr *ip_hdr = (struct iphdr *)buffer;
            if (ip_hdr->protocol != IPPROTO_UDP) continue;
            int ip_hdr_len = ip_hdr->ihl * 4;
            if (recv_len < ip_hdr_len + sizeof(struct udphdr)) continue;
            struct udphdr *udp_hdr = (struct udphdr *)(buffer + ip_hdr_len);
            uint16_t src_port = ntohs(udp_hdr->source);
            uint16_t dst_port = ntohs(udp_hdr->dest);
            uint32_t src_ip = ip_hdr->saddr;

            if (src_ip == server_addr.sin_addr.s_addr &&
                src_port == SERVER_PORT && dst_port == client_port) {
                char *data = buffer + ip_hdr_len + sizeof(struct udphdr);
                int data_len = ntohs(udp_hdr->len) - sizeof(struct udphdr);
                if (data_len < 0) continue;
                if (data_len >= BUFFER_SIZE) data_len = BUFFER_SIZE - 1;
                data[data_len] = '\0';
                printf("Server reply: \"%s\"\n", data);
            }
        }
    }

    send_close();
    usleep(100000);
    cleanup();
    printf("Client stopped.\n");
    return 0;
}
