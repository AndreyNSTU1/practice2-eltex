#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#define MAX_MESSAGES 10
#define MESSAGE_SIZE 1024


#define NORMAL_PRIORITY 1
#define END_PRIORITY    31

static volatile sig_atomic_t sigint_received = 0;


static void sigint_handler(int signo)
{
    (void)signo;
    sigint_received = 1;
}


static void make_timeout(struct timespec *ts, int seconds)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += seconds;
}


static int send_message(mqd_t queue,
                        const char *message,
                        unsigned int priority)
{
    struct timespec timeout;
    make_timeout(&timeout, 2);

    if (mq_timedsend(queue,
                     message,
                     strlen(message),
                     priority,
                     &timeout) == -1) {
        perror("mq_timedsend");
        return -1;
    }

    return 0;
}


static void send_end_message(mqd_t queue)
{
    printf("\nОтправка уведомления о завершении...\n");

    if (send_message(queue, "END", END_PRIORITY) == -1) {
        fprintf(stderr,
                "Не удалось отправить сообщение о завершении.\n");
    }
}


static mqd_t open_existing_queue(const char *name)
{
    int attempts = 30;

    while (attempts-- > 0) {
        mqd_t q = mq_open(name, O_RDWR);

        if (q != (mqd_t)-1)
            return q;

        if (errno != ENOENT)
            return (mqd_t)-1;

        struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 100000000L /* 100 мс */
        };

        nanosleep(&delay, NULL);
    }

    errno = ENOENT;
    return (mqd_t)-1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr,
                "Использование: %s <имя_очереди>\n"
                "Пример: %s /chat\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }


    char base_name[200];

    if (argv[1][0] == '/')
        snprintf(base_name, sizeof(base_name), "%s", argv[1]);
    else
        snprintf(base_name, sizeof(base_name), "/%s", argv[1]);

    char queue1_name[256];
    char queue2_name[256];

    snprintf(queue1_name, sizeof(queue1_name),
             "%s_1", base_name);

    snprintf(queue2_name, sizeof(queue2_name),
             "%s_2", base_name);

    struct mq_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MESSAGE_SIZE;
    attr.mq_curmsgs = 0;

    mqd_t queue1 = (mqd_t)-1;
    mqd_t queue2 = (mqd_t)-1;

    int creator = 0;


    queue1 = mq_open(queue1_name,
                     O_CREAT | O_EXCL | O_RDWR,
                     0666,
                     &attr);

    if (queue1 != (mqd_t)-1) {
        creator = 1;

        queue2 = mq_open(queue2_name,
                         O_CREAT | O_EXCL | O_RDWR,
                         0666,
                         &attr);

        if (queue2 == (mqd_t)-1) {
            perror("mq_open queue2");

            mq_close(queue1);
            mq_unlink(queue1_name);

            return EXIT_FAILURE;
        }

        printf("Очереди созданы текущим процессом.\n");
    }
    else {
        if (errno != EEXIST) {
            perror("mq_open queue1");
            return EXIT_FAILURE;
        }


        queue1 = open_existing_queue(queue1_name);

        if (queue1 == (mqd_t)-1) {
            perror("mq_open queue1");
            return EXIT_FAILURE;
        }

        queue2 = open_existing_queue(queue2_name);

        if (queue2 == (mqd_t)-1) {
            perror("mq_open queue2");
            mq_close(queue1);
            return EXIT_FAILURE;
        }

        printf("Подключение к существующим очередям.\n");
    }


    mqd_t receive_queue;
    mqd_t send_queue;

    if (creator) {
        receive_queue = queue1;
        send_queue = queue2;

        printf("Роль: процесс 1 (создатель)\n");
        printf("Прием:     %s\n", queue1_name);
        printf("Отправка:  %s\n", queue2_name);
    }
    else {
        send_queue = queue1;
        receive_queue = queue2;

        printf("Роль: процесс 2\n");
        printf("Отправка:  %s\n", queue1_name);
        printf("Прием:     %s\n", queue2_name);
    }


    struct mq_attr receive_attr;
    struct mq_attr send_attr;

    if (mq_getattr(receive_queue, &receive_attr) == -1) {
        perror("mq_getattr");
        goto cleanup;
    }

    if (mq_getattr(send_queue, &send_attr) == -1) {
        perror("mq_getattr");
        goto cleanup;
    }

    char *receive_buffer =
        malloc((size_t)receive_attr.mq_msgsize + 1);

    if (receive_buffer == NULL) {
        perror("malloc");
        goto cleanup;
    }


    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;

    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        free(receive_buffer);
        goto cleanup;
    }

    printf("\nЧат запущен.\n");
    printf("Введите сообщение и нажмите Enter.\n");
    printf("Для выхода: /quit или Ctrl+C\n\n");


    struct pollfd fds[2];

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = (int)receive_queue;
    fds[1].events = POLLIN;

    int running = 1;
    int need_send_end = 0;

    while (running) {
        if (sigint_received) {
            printf("\nПолучен SIGINT (Ctrl+C).\n");
            need_send_end = 1;
            break;
        }

        int result = poll(fds, 2, -1);

        if (result == -1) {
            if (errno == EINTR) {
                if (sigint_received) {
                    printf("\nПолучен SIGINT (Ctrl+C).\n");
                    need_send_end = 1;
                    break;
                }

                continue;
            }

            perror("poll");
            need_send_end = 1;
            break;
        }


        if (fds[1].revents & POLLIN) {
            unsigned int priority = 0;

            ssize_t bytes = mq_receive(
                receive_queue,
                receive_buffer,
                (size_t)receive_attr.mq_msgsize,
                &priority);

            if (bytes == -1) {
                if (errno != EINTR)
                    perror("mq_receive");
            }
            else {
                receive_buffer[bytes] = '\0';


                if (priority == END_PRIORITY) {
                    printf("\nСобеседник завершил работу.\n");
                    running = 0;
                    continue;
                }

                printf("\nСобеседник: %s\n", receive_buffer);
                printf("> ");
                fflush(stdout);
            }
        }


        if (running && (fds[0].revents & POLLIN)) {
            char input[MESSAGE_SIZE + 2];

            printf("> ");
            fflush(stdout);

            if (fgets(input, sizeof(input), stdin) == NULL) {
                
                printf("\nКонец ввода.\n");
                need_send_end = 1;
                break;
            }

            input[strcspn(input, "\n")] = '\0';

            if (strcmp(input, "/quit") == 0) {
                need_send_end = 1;
                break;
            }

            size_t len = strlen(input);

            if (len > (size_t)send_attr.mq_msgsize) {
                fprintf(stderr,
                        "Сообщение слишком длинное. "
                        "Максимум: %ld байт.\n",
                        send_attr.mq_msgsize);
                continue;
            }

            if (send_message(send_queue,
                             input,
                             NORMAL_PRIORITY) == -1) {
                fprintf(stderr,
                        "Ошибка отправки сообщения.\n");
            }
        }
    }


    if (need_send_end)
        send_end_message(send_queue);

    free(receive_buffer);

cleanup:

    if (queue1 != (mqd_t)-1)
        mq_close(queue1);

    if (queue2 != (mqd_t)-1)
        mq_close(queue2);


    if (creator) {
        printf("Удаление очередей...\n");

        if (mq_unlink(queue1_name) == -1 && errno != ENOENT)
            perror("mq_unlink queue1");

        if (mq_unlink(queue2_name) == -1 && errno != ENOENT)
            perror("mq_unlink queue2");
    }

    printf("Программа завершена.\n");

    return EXIT_SUCCESS;
}
