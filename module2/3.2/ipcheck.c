#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// преобразование IP строки в 32-битное число
unsigned int ip_to_int(const char *ip) {
    unsigned int a, b, c, d;
    sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d);
    return (a << 24) | (b << 16) | (c << 8) | d;
}

// генерация случайного IPv4
unsigned int random_ip() {
    return ((unsigned int)rand() << 16) ^ rand();
}

// вывод IP из числа
void print_ip(unsigned int ip) {
    printf("%u.%u.%u.%u",
        (ip >> 24) & 255,
        (ip >> 16) & 255,
        (ip >> 8) & 255,
        ip & 255
    );
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Использование: %s <gateway_ip> <mask> <N>\n", argv[0]);
        return 1;
    }

    srand(time(NULL));

    unsigned int gateway = ip_to_int(argv[1]);
    unsigned int mask = ip_to_int(argv[2]);
    int N = atoi(argv[3]);

    unsigned int network = gateway & mask;

    int same = 0;
    int other = 0;

    printf("Шлюзовая сеть: ");
    print_ip(network);
    printf("\n\n");

    for (int i = 0; i < N; i++) {
        unsigned int dst = random_ip();

        printf("%3d. ", i + 1);
        print_ip(dst);

        if ((dst & mask) == network) {
            printf(" -> своя подсеть\n");
            same++;
        } else {
            printf(" -> другая сеть\n");
            other++;
        }
    }

    printf("\n--- Статистика ---\n");
    printf("Своя подсеть: %d (%.2f%%)\n", same, (same * 100.0) / N);
    printf("Другая сеть : %d (%.2f%%)\n", other, (other * 100.0) / N);

    return 0;
}