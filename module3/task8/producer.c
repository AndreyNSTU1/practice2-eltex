#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>

#define MAX_NUMBERS 10
#define MAX_NUMBER_VALUE 100

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void generate_line(char *buffer, size_t size) {
    int count = rand() % MAX_NUMBERS + 1;
    char *ptr = buffer;
    int len = 0;
    for (int i = 0; i < count; i++) {
        int num = rand() % (MAX_NUMBER_VALUE + 1);
        len += snprintf(ptr + len, size - len, "%d ", num);
        if (len >= size - 1) break;
    }
    if (len > 0 && buffer[len-1] == ' ') buffer[len-1] = '\n';
    else buffer[len] = '\n';
    buffer[len] = '\0';
}

void sem_wait(int semid) {
    struct sembuf op = {0, -1, 0};
    if (semop(semid, &op, 1) == -1) {
        perror("semop wait");
        exit(EXIT_FAILURE);
    }
}

void sem_post(int semid) {
    struct sembuf op = {0, 1, 0};
    if (semop(semid, &op, 1) == -1) {
        perror("semop post");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <имя_файла>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *filename = argv[1];
    srand(time(NULL) ^ getpid());

    key_t key = ftok(filename, 1);
    if (key == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    int semid = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semid == -1) {
        if (errno == EEXIST) {
            semid = semget(key, 1, 0);
            if (semid == -1) {
                perror("semget");
                exit(EXIT_FAILURE);
            }
        } else {
            perror("semget");
            exit(EXIT_FAILURE);
        }
    } else {
        union semun arg;
        arg.val = 1;
        if (semctl(semid, 0, SETVAL, arg) == -1) {
            perror("semctl SETVAL");
            exit(EXIT_FAILURE);
        }
    }

    printf("Производитель запущен (файл: %s). Генерация строк...\n", filename);

    for (int i = 0; i < 5; i++) {
        char line[256];
        generate_line(line, sizeof(line));

        sem_wait(semid);
        FILE *file = fopen(filename, "a");
        if (file) {
            fputs(line, file);
            fclose(file);
            printf("Записано: %s", line);
            fflush(stdout);
        } else {
            perror("fopen");
        }
        sem_post(semid);

        sleep(rand() % 2 + 1);
    }

    return 0;
}
