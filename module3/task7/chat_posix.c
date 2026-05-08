#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>

#define MAX_MSG_SIZE 256
#define QUEUE_PERMS 0660

// Имена очередей
#define QUEUE_A_TO_B "/chat_a_to_b"
#define QUEUE_B_TO_A "/chat_b_to_a"

void cleanup(mqd_t mq1, mqd_t mq2) {
    if (mq1 != (mqd_t)-1) mq_close(mq1);
    if (mq2 != (mqd_t)-1) mq_close(mq2);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <A|B>\n", argv[0]);
        fprintf(stderr, "A — отправляет первым, B — ждёт сообщения\n");
        exit(EXIT_FAILURE);
    }

    char role = argv[1][0];
    if (role != 'A' && role != 'B') {
        fprintf(stderr, "Роль должна быть A или B\n");
        exit(EXIT_FAILURE);
    }

    const char *send_path, *recv_path;

    if (role == 'A') {
        // A отправляет в очередь B, получает из очереди B->A
        send_path = QUEUE_A_TO_B;
        recv_path = QUEUE_B_TO_A;
    } else { // role == 'B'
        send_path = QUEUE_B_TO_A;
        recv_path = QUEUE_A_TO_B;
    }

    // Настройка атрибутов очередей
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    // Очередь для отправки (создаём, если её нет)
    mqd_t mq_send_fd = mq_open(send_path, O_CREAT | O_WRONLY, QUEUE_PERMS, &attr);
    if (mq_send_fd == (mqd_t)-1) {
        perror("mq_open (send)");
        exit(EXIT_FAILURE);
    }

    // Очередь для приёма (создаём, если её нет)
    mqd_t mq_recv_fd = mq_open(recv_path, O_CREAT | O_RDONLY, QUEUE_PERMS, &attr);
    if (mq_recv_fd == (mqd_t)-1) {
        perror("mq_open (recv)");
        mq_close(mq_send_fd);
        exit(EXIT_FAILURE);
    }

    printf("Чат запущен. Роль %c\n", role);

    char buffer[MAX_MSG_SIZE];
    unsigned int priority;

    if (role == 'A') {
        // A отправляет первое сообщение
        printf("Введите сообщение (или 'QUIT' для выхода): ");
        fflush(stdout);
        if (fgets(buffer, MAX_MSG_SIZE, stdin) == NULL) {
            cleanup(mq_send_fd, mq_recv_fd);
            exit(0);
        }
        buffer[strcspn(buffer, "\n")] = '\0';

        if (mq_send(mq_send_fd, buffer, strlen(buffer) + 1, 0) == -1) {
            perror("mq_send");
            cleanup(mq_send_fd, mq_recv_fd);
            exit(EXIT_FAILURE);
        }
        if (strcmp(buffer, "QUIT") == 0) {
            printf("Завершение по команде QUIT\n");
            cleanup(mq_send_fd, mq_recv_fd);
            exit(0);
        }
    }

    // Основной цикл: получить сообщение, отправить ответ (пинг-понг)
    while (1) {
        // Получение сообщения
        ssize_t bytes = mq_receive(mq_recv_fd, buffer, MAX_MSG_SIZE, &priority);
        if (bytes == -1) {
            perror("mq_receive");
            break;
        }
        buffer[bytes] = '\0';
        printf("\nПолучено: %s\n", buffer);

        if (strcmp(buffer, "QUIT") == 0) {
            printf("Партнёр завершил чат. Выход.\n");
            break;
        }

        // Ввод ответа
        printf("Ответ (или 'QUIT' для выхода): ");
        fflush(stdout);
        if (fgets(buffer, MAX_MSG_SIZE, stdin) == NULL) break;
        buffer[strcspn(buffer, "\n")] = '\0';

        if (mq_send(mq_send_fd, buffer, strlen(buffer) + 1, 0) == -1) {
            perror("mq_send");
            break;
        }

        if (strcmp(buffer, "QUIT") == 0) {
            printf("Завершение по команде QUIT.\n");
            break;
        }
    }

    cleanup(mq_send_fd, mq_recv_fd);
    return 0;
}
