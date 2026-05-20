#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>

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

void process_line(const char *line) {
    int num, min, max, first = 1;
    const char *p = line;
    while (1) {
        char *end;
        num = strtol(p, &end, 10);
        if (p == end) break;
        if (first) {
            min = max = num;
            first = 0;
        } else {
            if (num < min) min = num;
            if (num > max) max = num;
        }
        p = end;
    }
    if (!first) {
        printf("[%d] Минимум: %d, Максимум: %d\n", getpid(), min, max);
        fflush(stdout);
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

    int semid = semget(key, 1, 0);
    if (semid == -1) {
        perror("semget (запустите сначала производителя)");
        exit(EXIT_FAILURE);
    }

    printf("Потребитель %d запущен (файл: %s). Ожидание строк...\n", getpid(), filename);

    while (1) {
        sem_wait(semid);

        FILE *file = fopen(filename, "r+");
        if (!file) {
            sem_post(semid);
            perror("fopen");
            sleep(1);
            continue;
        }

        char first_line[256];
        if (fgets(first_line, sizeof(first_line), file) == NULL) {
            fclose(file);
            sem_post(semid);
            sleep(1);
            continue;
        }

        char rest[1024 * 10];
        size_t rest_len = fread(rest, 1, sizeof(rest) - 1, file);
        rest[rest_len] = '\0';
        fclose(file);

        file = fopen(filename, "w");
        if (file) {
            fwrite(rest, 1, rest_len, file);
            fclose(file);
        } else {
            perror("fopen write");
        }

        sem_post(semid);

        process_line(first_line);
        sleep(rand() % 2);
    }

    return 0;
}
