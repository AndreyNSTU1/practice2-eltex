#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 65536

// Разбор UDP-пакета: выводим данные
void process_udp_packet(const unsigned char *packet, int packet_len, int target_port) {
    struct iphdr *ip = (struct iphdr *)packet;
    unsigned int ip_header_len = ip->ihl * 4;
    
    // Проверяем, что это UDP (протокол 17)
    if (ip->protocol != IPPROTO_UDP) return;
    
    // Смещение до UDP-заголовка
    struct udphdr *udp = (struct udphdr *)(packet + ip_header_len);
    
    // Проверяем порт назначения
    int dest_port = ntohs(udp->dest);
    if (dest_port != target_port) return;
    
    // Данные начинаются после UDP-заголовка (8 байт)
    unsigned char *data = (unsigned char *)udp + sizeof(struct udphdr);
    int data_len = packet_len - ip_header_len - sizeof(struct udphdr);
    if (data_len <= 0) return;
    
    // Выводим информацию
    char src_ip[INET_ADDRSTRLEN];
    struct sockaddr_in src_addr;
    src_addr.sin_addr.s_addr = ip->saddr;
    inet_ntop(AF_INET, &src_addr.sin_addr, src_ip, INET_ADDRSTRLEN);
    
    printf("[%s:%d -> localhost:%d] ", src_ip, ntohs(udp->source), dest_port);
    fwrite(data, 1, data_len, stdout);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <порт_сервера>\n", argv[0]);
        fprintf(stderr, "Пример: %s 5000\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    int target_port = atoi(argv[1]);
    
    // Создаём RAW-сокет для перехвата UDP-пакетов
    int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (raw_sock < 0) {
        perror("socket (требуются права root)");
        exit(EXIT_FAILURE);
    }
    
    printf("Снифер запущен. Перехватываю UDP-пакеты на порт %d\n", target_port);
    printf("Нажмите Ctrl+C для выхода.\n\n");
    
    unsigned char buffer[BUFFER_SIZE];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    
    while (1) {
        int packet_len = recvfrom(raw_sock, buffer, BUFFER_SIZE, 0,
                                  (struct sockaddr*)&sender, &sender_len);
        if (packet_len < 0) {
            perror("recvfrom");
            continue;
        }
        process_udp_packet(buffer, packet_len, target_port);
    }
    
    close(raw_sock);
    return 0;
}
