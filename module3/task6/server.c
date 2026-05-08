#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>
#include <unistd.h>

#define MSG_KEY_PATH "."   // текущий каталог
#define MSG_PROJ_ID   'C'  // идентификатор проекта
#define MAX_CLIENTS   32
#define MSG_SIZE      256

struct msgbuf {
    long mtype;
    char mtext[MSG_SIZE];
};

int msgid = -1;
int active_clients[MAX_CLIENTS]; // номера активных клиентов
int active_count = 0;

// Удалить очередь при завершении
void cleanup(int sig) {
    if (msgid != -1) {
        msgctl(msgid, IPC_RMID, NULL);
        printf("\nСервер завершён, очередь удалена.\n");
    }
    exit(0);
}

// Добавить клиента в список, если его ещё нет
void add_client(int num) {
    for (int i = 0; i < active_count; i++) {
        if (active_clients[i] == num) return;
    }
    if (active_count < MAX_CLIENTS) {
        active_clients[active_count++] = num;
        printf("Клиент %d подключился. Активных: %d\n", num, active_count);
    } else {
        printf("Превышен лимит клиентов, %d не добавлен\n", num);
    }
}

// Удалить клиента из списка
void remove_client(int num) {
    for (int i = 0; i < active_count; i++) {
        if (active_clients[i] == num) {
            for (int j = i; j < active_count - 1; j++)
                active_clients[j] = active_clients[j + 1];
            active_count--;
            printf("Клиент %d отключён. Активных: %d\n", num, active_count);
            return;
        }
    }
}

int main() {
    key_t key = ftok(MSG_KEY_PATH, MSG_PROJ_ID);
    if (key == -1) {
        perror("ftok");
        exit(1);
    }

    msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        exit(1);
    }

    signal(SIGINT, cleanup);

    printf("Сервер запущен, очередь создана (ID %d). Ожидание сообщений...\n", msgid);

    struct msgbuf rcvbuf, sndbuf;
    while (1) {
        // Принимаем сообщение от любого клиента (тип = 10)
        if (msgrcv(msgid, &rcvbuf, MSG_SIZE, 10, 0) == -1) {
            perror("msgrcv");
            continue;
        }

        // Разбираем строку "номер:текст"
        char *colon = strchr(rcvbuf.mtext, ':');
        if (!colon) {
            printf("Некорректный формат сообщения: %s\n", rcvbuf.mtext);
            continue;
        }
        *colon = '\0';
        int sender = atoi(rcvbuf.mtext);
        char *text = colon + 1;
        *colon = ':'; // восстановим для пересылки

        // Обработка shutdown
        if (strcmp(text, "shutdown") == 0) {
            remove_client(sender);
            continue;
        }

        // Добавляем отправителя (если новый)
        add_client(sender);

        // Пересылаем сообщение всем активным клиентам, кроме отправителя
        for (int i = 0; i < active_count; i++) {
            int receiver = active_clients[i];
            if (receiver == sender) continue;

            sndbuf.mtype = receiver;
            strcpy(sndbuf.mtext, rcvbuf.mtext); // исходная строка "отправитель:текст"
            if (msgsnd(msgid, &sndbuf, strlen(sndbuf.mtext) + 1, 0) == -1) {
                perror("msgsnd");
                // Возможно, клиент умер – для простоты игнорируем
            }
        }

        printf("Переслано от %d: %s\n", sender, text);
    }

    return 0;
}
