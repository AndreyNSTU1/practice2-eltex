#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/select.h>

#define MSG_KEY_PATH "."
#define MSG_PROJ_ID  'C'
#define MSG_SIZE     256

struct msgbuf {
    long mtype;
    char mtext[MSG_SIZE];
};

int msgid;
int my_num;

void send_message(const char *text) {
    struct msgbuf buf;
    buf.mtype = 10;  // всегда серверу
    snprintf(buf.mtext, MSG_SIZE, "%d:%s", my_num, text);
    if (msgsnd(msgid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
        perror("msgsnd");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <номер_клиента>\n", argv[0]);
        fprintf(stderr, "Номер должен быть 20,30,40,... (не 10)\n");
        exit(1);
    }

    my_num = atoi(argv[1]);
    if (my_num == 10 || my_num % 10 != 0) {
        fprintf(stderr, "Номер клиента должен быть кратен 10 и не равен 10\n");
        exit(1);
    }

    key_t key = ftok(MSG_KEY_PATH, MSG_PROJ_ID);
    if (key == -1) {
        perror("ftok");
        exit(1);
    }

    // Подключаемся к существующей очереди
    msgid = msgget(key, 0666);
    if (msgid == -1) {
        perror("msgget (возможно, сервер не запущен)");
        exit(1);
    }

    printf("Клиент %d запущен. Введите сообщения (shutdown для выхода)\n", my_num);

    fd_set readfds;
    struct timeval tv;
    struct msgbuf rcvbuf;
    char input[MSG_SIZE];

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 1;   // таймаут 1 секунда для возможности проверить очередь
        tv.tv_usec = 0;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        // Ввод с клавиатуры
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(input, MSG_SIZE, stdin) == NULL) break;
            input[strcspn(input, "\n")] = '\0'; // убираем \n

            if (strcmp(input, "shutdown") == 0) {
                send_message("shutdown");
                printf("Отключение...\n");
                break;
            } else {
                send_message(input);
            }
        }

        // Неблокирующая проверка личных сообщений
        while (msgrcv(msgid, &rcvbuf, MSG_SIZE, my_num, IPC_NOWAIT) != -1) {
            printf("%s\n", rcvbuf.mtext);
            fflush(stdout);
        }
    }

    return 0;
}
