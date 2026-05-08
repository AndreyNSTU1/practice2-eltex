#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>

// Извлекает из строки минимальное и максимальное число
void process_line(const char *line) {
    int num, min, max, first = 1;
    const char *p = line;
    while (1) {
        char *end;
        num = strtol(p, &end, 10);
        if (p == end) break; // чисел больше нет
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

    // Имя семафора, как у производителя
    char sem_name[256];
    snprintf(sem_name, sizeof(sem_name), "/sem_%s", filename);
    for (char *p = sem_name; *p; p++) if (*p == '/') *p = '_';

    sem_t *sem = sem_open(sem_name, O_CREAT, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    printf("Потребитель %d запущен (файл: %s). Ожидание строк...\n", getpid(), filename);

    while (1) {
        sem_wait(sem); // захватываем файл

        FILE *file = fopen(filename, "r+");
        if (!file) {
            sem_post(sem);
            perror("fopen");
            sleep(1);
            continue;
        }

        // Проверяем, есть ли хотя бы одна строка
        char first_line[256];
        if (fgets(first_line, sizeof(first_line), file) == NULL) {
            // Файл пуст
            fclose(file);
            sem_post(sem);
            sleep(1);
            continue;
        }

        // Читаем оставшуюся часть файла
        char rest[1024 * 10];
        size_t rest_len = fread(rest, 1, sizeof(rest) - 1, file);
        rest[rest_len] = '\0';
        fclose(file);

        // Перезаписываем файл только оставшейся частью (удаляем первую строку)
        file = fopen(filename, "w");
        if (file) {
            fwrite(rest, 1, rest_len, file);
            fclose(file);
        } else {
            perror("fopen write");
        }

        sem_post(sem); // освобождаем файл

        // Обрабатываем удалённую строку
        process_line(first_line);

        sleep(rand() % 2); // небольшая задержка
    }

    sem_close(sem);
    return 0;
}
