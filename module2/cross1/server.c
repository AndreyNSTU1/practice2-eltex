// server.c
// Echo-server on raw sockets (UDP)
// Compile: gcc -Wall -Wextra -o server server.c
// Run: sudo ./server

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
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <netinet/if_ether.h>

#define SERVER_PORT 8888
#define BUFFER_SIZE 65536

// Client entry
typedef struct client_entry {
    uint32_t ip;
    uint16_t port;
    int counter;
    struct client_entry *next;
} client_entry_t;

client_entry_t *clients = NULL;
volatile sig_atomic_t running = 1;
int raw_sock = -1;

// Prototypes
uint16_t checksum(void *data, int len);
void add_client(uint32_t ip, uint16_t port);
client_entry_t *find_client(uint32_t ip, uint16_t port);
void remove_client(uint32_t ip, uint16_t port);
void handle_signal(int sig);
void cleanup();
void process_packet(char *packet, int len);

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

void add_client(uint32_t ip, uint16_t port) {
    client_entry_t *new = malloc(sizeof(client_entry_t));
    new->ip = ip;
    new->port = port;
    new->counter = 1;
    new->next = clients;
    clients = new;
    printf("New client %s:%u, counter set to 1\n",
           inet_ntoa(*(struct in_addr *)&ip), ntohs(port));
}

client_entry_t *find_client(uint32_t ip, uint16_t port) {
    client_entry_t *cur = clients;
    while (cur) {
        if (cur->ip == ip && cur->port == port)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

void remove_client(uint32_t ip, uint16_t port) {
    client_entry_t **ptr = &clients;
    while (*ptr) {
        if ((*ptr)->ip == ip && (*ptr)->port == port) {
            client_entry_t *tmp = *ptr;
            *ptr = tmp->next;
            printf("Removed client %s:%u\n",
                   inet_ntoa(*(struct in_addr *)&ip), ntohs(port));
            free(tmp);
            return;
        }
        ptr = &((*ptr)->next);
    }
}

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\nShutting down server...\n");
        running = 0;
    }
}

void cleanup() {
    if (raw_sock >= 0)
        close(raw_sock);
    while (clients) {
        client_entry_t *tmp = clients;
        clients = clients->next;
        free(tmp);
    }
}

void process_packet(char *packet, int len) {
    struct iphdr *ip_hdr = (struct iphdr *)packet;
    if (ip_hdr->protocol != IPPROTO_UDP)
        return;

    int ip_hdr_len = ip_hdr->ihl * 4;
    if (len < ip_hdr_len + sizeof(struct udphdr))
        return;

    struct udphdr *udp_hdr = (struct udphdr *)(packet + ip_hdr_len);
    uint16_t dst_port = ntohs(udp_hdr->dest);
    if (dst_port != SERVER_PORT)
        return;

    uint16_t src_port = ntohs(udp_hdr->source);
    uint32_t src_ip = ip_hdr->saddr;

    char *data = packet + ip_hdr_len + sizeof(struct udphdr);
    int data_len = ntohs(udp_hdr->len) - sizeof(struct udphdr);
    if (data_len <= 0)
        return;

    char message[BUFFER_SIZE];
    if (data_len >= BUFFER_SIZE)
        data_len = BUFFER_SIZE - 1;
    memcpy(message, data, data_len);
    message[data_len] = '\0';

    printf("Received from %s:%u: \"%s\"\n",
           inet_ntoa(*(struct in_addr *)&src_ip), src_port, message);

    if (strcmp(message, "CLOSE") == 0) {
        remove_client(src_ip, htons(src_port));
        return;
    }

    client_entry_t *client = find_client(src_ip, htons(src_port));
    if (!client) {
        add_client(src_ip, htons(src_port));
        client = find_client(src_ip, htons(src_port));
    } else {
        client->counter++;
    }

    char response[BUFFER_SIZE];
    int resp_len = snprintf(response, sizeof(response), "%s %d",
                            message, client->counter);
    if (resp_len >= BUFFER_SIZE)
        resp_len = BUFFER_SIZE - 1;

    // Build IP header
    struct iphdr resp_ip;
    memset(&resp_ip, 0, sizeof(resp_ip));
    resp_ip.version = 4;
    resp_ip.ihl = 5;
    resp_ip.tot_len = htons(sizeof(resp_ip) + sizeof(struct udphdr) + resp_len);
    resp_ip.id = htons(rand() & 0xFFFF);
    resp_ip.ttl = 64;
    resp_ip.protocol = IPPROTO_UDP;
    resp_ip.saddr = htonl(INADDR_ANY);
    resp_ip.daddr = src_ip;
    resp_ip.check = checksum(&resp_ip, sizeof(resp_ip));

    struct udphdr resp_udp;
    resp_udp.source = htons(SERVER_PORT);
    resp_udp.dest = htons(src_port);
    resp_udp.len = htons(sizeof(resp_udp) + resp_len);
    resp_udp.check = 0;

    struct pseudo_header {
        uint32_t src_addr;
        uint32_t dst_addr;
        uint8_t zero;
        uint8_t protocol;
        uint16_t udp_len;
    } pseudo;
    pseudo.src_addr = resp_ip.saddr;
    pseudo.dst_addr = resp_ip.daddr;
    pseudo.zero = 0;
    pseudo.protocol = IPPROTO_UDP;
    pseudo.udp_len = resp_udp.len;

    char udp_buf[sizeof(pseudo) + sizeof(resp_udp) + resp_len];
    memcpy(udp_buf, &pseudo, sizeof(pseudo));
    memcpy(udp_buf + sizeof(pseudo), &resp_udp, sizeof(resp_udp));
    memcpy(udp_buf + sizeof(pseudo) + sizeof(resp_udp), response, resp_len);
    resp_udp.check = checksum(udp_buf, sizeof(pseudo) + sizeof(resp_udp) + resp_len);

    char packet_buf[BUFFER_SIZE];
    memcpy(packet_buf, &resp_ip, sizeof(resp_ip));
    memcpy(packet_buf + sizeof(resp_ip), &resp_udp, sizeof(resp_udp));
    memcpy(packet_buf + sizeof(resp_ip) + sizeof(resp_udp), response, resp_len);

    struct sockaddr_in dest_in;
    dest_in.sin_family = AF_INET;
    dest_in.sin_addr.s_addr = src_ip;
    dest_in.sin_port = 0;

    int sent = sendto(raw_sock, packet_buf, ntohs(resp_ip.tot_len), 0,
                      (struct sockaddr *)&dest_in, sizeof(dest_in));
    if (sent < 0) {
        perror("sendto");
    } else {
        printf("Replied to %s:%u: \"%s\"\n",
               inet_ntoa(*(struct in_addr *)&src_ip), src_port, response);
    }
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (raw_sock < 0) {
        perror("socket");
        fprintf(stderr, "Need root privileges.\n");
        return 1;
    }

    int one = 1;
    if (setsockopt(raw_sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt IP_HDRINCL");
        close(raw_sock);
        return 1;
    }

    printf("Echo server (raw UDP) listening on port %d\n", SERVER_PORT);
    printf("Press Ctrl+C to stop.\n");

    char buffer[BUFFER_SIZE];
    while (running) {
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);
        int recv_len = recvfrom(raw_sock, buffer, BUFFER_SIZE, 0,
                                (struct sockaddr *)&src_addr, &addr_len);
        if (recv_len < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }
        process_packet(buffer, recv_len);
    }

    cleanup();
    printf("Server stopped.\n");
    return 0;
}
