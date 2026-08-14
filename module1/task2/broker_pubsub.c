#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <stdarg.h>

#define MAX_TEXT 1024
#define KEY_FILE "."
#define KEY_PROJ 'B'

/* Структура сообщения для очереди System V */
struct msgbuf {
    long mtype;
    char mtext[MAX_TEXT];
};

/* Глобальные переменные для брокера */
int msgid = -1;
volatile sig_atomic_t exit_flag = 0;

/* Список издателей (PID) */
int *publishers = NULL;
int pub_count = 0;

/* Структура подписчика */
typedef struct subscriber {
    pid_t pid;
    char **topics;          /* массив названий тем */
    int topic_count;
} subscriber_t;

subscriber_t *subscribers = NULL;
int sub_count = 0;

/* Обработчик сигнала SIGINT (для всех режимов) */
void sigint_handler(int sig) {
    (void)sig;
    exit_flag = 1;
}

/* ---------- Вспомогательные функции для брокера ---------- */

/* Добавить PID издателя (если ещё не добавлен) */
void add_publisher(pid_t pid) {
    for (int i = 0; i < pub_count; i++) {
        if (publishers[i] == pid) return;
    }
    publishers = realloc(publishers, (pub_count + 1) * sizeof(int));
    if (!publishers) { perror("realloc publishers"); exit(1); }
    publishers[pub_count++] = pid;
}

/* Найти подписчика по PID (возвращает индекс или -1) */
int find_subscriber(pid_t pid) {
    for (int i = 0; i < sub_count; i++) {
        if (subscribers[i].pid == pid) return i;
    }
    return -1;
}

/* Добавить тему подписчику */
void add_subscription(pid_t pid, const char *topic) {
    int idx = find_subscriber(pid);
    if (idx == -1) {
        /* Новый подписчик */
        subscribers = realloc(subscribers, (sub_count + 1) * sizeof(subscriber_t));
        if (!subscribers) { perror("realloc subscribers"); exit(1); }
        idx = sub_count++;
        subscribers[idx].pid = pid;
        subscribers[idx].topics = NULL;
        subscribers[idx].topic_count = 0;
    }
    /* Проверим, не подписан ли уже на эту тему */
    subscriber_t *s = &subscribers[idx];
    for (int i = 0; i < s->topic_count; i++) {
        if (strcmp(s->topics[i], topic) == 0) return;
    }
    /* Добавляем тему */
    s->topics = realloc(s->topics, (s->topic_count + 1) * sizeof(char *));
    if (!s->topics) { perror("realloc topics"); exit(1); }
    s->topics[s->topic_count] = malloc(strlen(topic) + 1);
    if (!s->topics[s->topic_count]) { perror("malloc topic"); exit(1); }
    strcpy(s->topics[s->topic_count], topic);
    s->topic_count++;
}

/* Удалить подписку на тему */
void remove_subscription(pid_t pid, const char *topic) {
    int idx = find_subscriber(pid);
    if (idx == -1) return;
    subscriber_t *s = &subscribers[idx];
    for (int i = 0; i < s->topic_count; i++) {
        if (strcmp(s->topics[i], topic) == 0) {
            free(s->topics[i]);
            /* Сдвигаем оставшиеся */
            for (int j = i; j < s->topic_count - 1; j++) {
                s->topics[j] = s->topics[j+1];
            }
            s->topic_count--;
            break;
        }
    }
    /* Если тем не осталось, удаляем подписчика */
    if (s->topic_count == 0) {
        free(s->topics);
        /* Сдвигаем в массиве subscribers */
        for (int i = idx; i < sub_count - 1; i++) {
            subscribers[i] = subscribers[i+1];
        }
        sub_count--;
    }
}

/* Переслать сообщение всем подписчикам на заданную тему */
void forward_message(const char *full_msg, const char *topic) {
    struct msgbuf outbuf;
    outbuf.mtype = 0; /* временно */
    strncpy(outbuf.mtext, full_msg, MAX_TEXT - 1);
    outbuf.mtext[MAX_TEXT - 1] = '\0';

    for (int i = 0; i < sub_count; i++) {
        subscriber_t *s = &subscribers[i];
        for (int j = 0; j < s->topic_count; j++) {
            if (strcmp(s->topics[j], topic) == 0) {
                outbuf.mtype = s->pid;
                if (msgsnd(msgid, &outbuf, strlen(outbuf.mtext) + 1, 0) == -1) {
                    /* Если подписчик уже не существует, просто игнорируем ошибку */
                    if (errno != EINVAL && errno != EIDRM) {
                        perror("msgsnd forward");
                    }
                }
                break;
            }
        }
    }
}

/* Обработка одного сообщения из очереди (mtype == 1) */
void process_message(const char *text) {
    char cmd[20];
    pid_t pid;
    char topic[64];
    char payload[MAX_TEXT];

    /* Парсим команду */
    if (sscanf(text, "subscribe,%d,%63[^,\n]", &pid, topic) == 2) {
        add_subscription(pid, topic);
        return;
    }
    if (sscanf(text, "unsubscribe,%d,%63[^,\n]", &pid, topic) == 2) {
        remove_subscription(pid, topic);
        return;
    }
    if (sscanf(text, "send,%d,%63[^,],%[^\n]", &pid, topic, payload) == 3) {
        /* Издатель */
        add_publisher(pid);
        /* Пересылаем полное сообщение (включая "send,...") всем подписчикам */
        forward_message(text, topic);
        return;
    }
    /* Неизвестная команда – игнорируем */
}

/* Очистка брокера перед завершением */
void cleanup_broker(void) {
    /* Посылаем SIGINT всем издателям и подписчикам */
    for (int i = 0; i < pub_count; i++) {
        kill(publishers[i], SIGINT);
    }
    for (int i = 0; i < sub_count; i++) {
        kill(subscribers[i].pid, SIGINT);
    }
    /* Удаляем очередь сообщений */
    if (msgid != -1) {
        msgctl(msgid, IPC_RMID, NULL);
        msgid = -1;
    }
    /* Освобождаем память (не критично, но для порядка) */
    free(publishers);
    for (int i = 0; i < sub_count; i++) {
        subscriber_t *s = &subscribers[i];
        for (int j = 0; j < s->topic_count; j++) {
            free(s->topics[j]);
        }
        free(s->topics);
    }
    free(subscribers);
}

/* Режим брокера */
void broker_mode(void) {
    key_t key = ftok(KEY_FILE, KEY_PROJ);
    if (key == -1) {
        perror("ftok");
        exit(1);
    }

    /* Пытаемся создать очередь с исключительным доступом */
    msgid = msgget(key, IPC_CREAT | IPC_EXCL | 0666);
    if (msgid == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "Очередь сообщений уже существует. Брокер уже запущен.\n");
            exit(1);
        } else {
            perror("msgget");
            exit(1);
        }
    }

    signal(SIGINT, sigint_handler);

    struct msgbuf buf;
    while (!exit_flag) {
        ssize_t n = msgrcv(msgid, &buf, MAX_TEXT, 1, 0);
        if (n == -1) {
            if (errno == EINTR) continue;  /* сигнал */
            perror("msgrcv");
            break;
        }
        buf.mtext[n] = '\0'; /* безопасность */
        process_message(buf.mtext);
    }

    cleanup_broker();
}

/* ---------- Режим издателя ---------- */
void publisher_mode(const char *topic) {
    key_t key = ftok(KEY_FILE, KEY_PROJ);
    if (key == -1) { perror("ftok"); exit(1); }

    msgid = msgget(key, 0);
    if (msgid == -1) {
        perror("msgget: очередь не существует");
        exit(1);
    }

    pid_t mypid = getpid();
    signal(SIGINT, sigint_handler);

    struct msgbuf buf;
    buf.mtype = 1;
    char input[MAX_TEXT];

    printf("Издатель (тема '%s'). Вводите сообщения (exit - завершить):\n", topic);
    while (!exit_flag) {
        if (!fgets(input, sizeof(input), stdin)) break;
        /* Убираем символ новой строки */
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "exit") == 0) break;

        snprintf(buf.mtext, MAX_TEXT, "send,%d,%s,%s", mypid, topic, input);
        if (msgsnd(msgid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) {
                fprintf(stderr, "Очередь удалена, завершаем работу.\n");
                break;
            }
            perror("msgsnd");
            break;
        }
    }
}

/* ---------- Режим подписчика ---------- */
void subscriber_mode(char *topics[], int topic_count) {
    if (topic_count == 0) {
        fprintf(stderr, "Не указаны темы для подписки.\n");
        exit(1);
    }

    key_t key = ftok(KEY_FILE, KEY_PROJ);
    if (key == -1) { perror("ftok"); exit(1); }

    msgid = msgget(key, 0);
    if (msgid == -1) {
        perror("msgget: очередь не существует");
        exit(1);
    }

    pid_t mypid = getpid();
    signal(SIGINT, sigint_handler);

    /* Подписываемся на все указанные темы */
    struct msgbuf sub_buf;
    sub_buf.mtype = 1;
    for (int i = 0; i < topic_count; i++) {
        snprintf(sub_buf.mtext, MAX_TEXT, "subscribe,%d,%s", mypid, topics[i]);
        if (msgsnd(msgid, &sub_buf, strlen(sub_buf.mtext) + 1, 0) == -1) {
            perror("msgsnd subscribe");
            exit(1);
        }
    }

    printf("Подписчик (PID=%d) подписался на темы: ", mypid);
    for (int i = 0; i < topic_count; i++) printf("%s ", topics[i]);
    printf("\nОжидание сообщений...\n");

    struct msgbuf recv_buf;
    while (!exit_flag) {
        ssize_t n = msgrcv(msgid, &recv_buf, MAX_TEXT, mypid, 0);
        if (n == -1) {
            if (errno == EINTR) continue;
            if (errno == EIDRM || errno == EINVAL) {
                fprintf(stderr, "Очередь удалена, завершаем работу.\n");
                break;
            }
            perror("msgrcv");
            break;
        }
        recv_buf.mtext[n] = '\0';
        printf("Получено: %s\n", recv_buf.mtext);
    }

    /* Отписываемся от всех тем */
    sub_buf.mtype = 1;
    for (int i = 0; i < topic_count; i++) {
        snprintf(sub_buf.mtext, MAX_TEXT, "unsubscribe,%d,%s", mypid, topics[i]);
        msgsnd(msgid, &sub_buf, strlen(sub_buf.mtext) + 1, 0);
        /* ошибки игнорируем, т.к. брокер мог уже завершиться */
    }
}

/* ---------- Главная функция ---------- */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Использование:\n");
        fprintf(stderr, "  %s -b                (брокер)\n", argv[0]);
        fprintf(stderr, "  %s -p <topic>        (издатель)\n", argv[0]);
        fprintf(stderr, "  %s -s <topic1> ...   (подписчик)\n", argv[0]);
        exit(1);
    }

    if (strcmp(argv[1], "-b") == 0) {
        broker_mode();
    } else if (strcmp(argv[1], "-p") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Укажите тему для издателя.\n");
            exit(1);
        }
        publisher_mode(argv[2]);
    } else if (strcmp(argv[1], "-s") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Укажите хотя бы одну тему для подписки.\n");
            exit(1);
        }
        /* Передаём темы начиная с argv[2] */
        subscriber_mode(&argv[2], argc - 2);
    } else {
        fprintf(stderr, "Неизвестный ключ: %s\n", argv[1]);
        exit(1);
    }

    return 0;
}
