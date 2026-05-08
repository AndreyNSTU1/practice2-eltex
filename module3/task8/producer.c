#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>

#define MAX_NUMBERS 10
#define MAX_NUMBER_VALUE 100

// Генерация случайной строки из случайного количества случайных чисел
void generate_line(char *buffer, size_t size) {
    int count = rand() % MAX_NUMBERS + 1; // от 1 до MAX_NUMBERS
    char *ptr = buffer;
    int len = 0;
    for (int i = 0; i < count; i++) {
        int num = rand() % (MAX_NUMBER_VALUE + 1);
        len += snprintf(ptr + len, size - len, "%d ", num);
        if (len >= size - 1) break;
    }
    // Заменяем последний пробел на '\n'
    if (len > 0 && buffer[len-1] == ' ') buffer[len-1] = '\n';
    else buffer[len] = '\n';
    buffer[len] = '\0';
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <имя_файла>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *filename = argv[1];
    srand(time(NULL) ^ getpid()); // уникальный seed для каждого процесса

    // Формируем имя семафора на основе имени файла
    char sem_name[256];
    snprintf(sem_name, sizeof(sem_name), "/sem_%s", filename);
    // Заменяем '/' в имени файла на '_', т.к. слэш недопустим в имени семафора
    for (char *p = sem_name; *p; p++) if (*p == '/') *p = '_';

    sem_t *sem = sem_open(sem_name, O_CREAT, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    printf("Производитель запущен (файл: %s). Генерация строк...\n", filename);
    for (int i = 0; i < 5; i++) { // для примера генерируем 5 строк, можно зациклить
        char line[256];
        generate_line(line, sizeof(line));

        // Критическая секция – запись в файл
        sem_wait(sem);
        FILE *file = fopen(filename, "a");
        if (file) {
            fputs(line, file);
            fclose(file);
            printf("Записано: %s", line);
        } else {
            perror("fopen");
        }
        sem_post(sem);

        sleep(rand() % 2 + 1); // пауза между генерациями
    }

    sem_close(sem);
    // Не удаляем семафор, он может понадобиться другим процессам
    return 0;
}
