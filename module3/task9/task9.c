#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>

#define MAX_NUMBERS 10      // максимальное количество чисел в строке
#define MAX_VALUE    100    // максимальное значение числа
#define LINES_COUNT  8      // сколько строк сгенерирует родитель
#define FILENAME     "data.txt"
#define SEM_NAME     "/sem_data"

// Генерация одной строки: случайное количество случайных чисел, разделённых пробелами
void generate_line(char *buffer, size_t size) {
    int count = rand() % MAX_NUMBERS + 1;  // от 1 до MAX_NUMBERS
    char *ptr = buffer;
    int len = 0;

    for (int i = 0; i < count; ++i) {
        int num = rand() % (MAX_VALUE + 1);
        len += snprintf(ptr + len, size - len, "%d ", num);
        if (len >= size - 1) break;
    }
    // заменяем последний пробел на '\n'
    if (len > 0 && buffer[len-1] == ' ')
        buffer[len-1] = '\n';
    else
        buffer[len] = '\n';
    buffer[len] = '\0';
}

// Обработка строки: поиск минимального и максимального числа
void process_line(const char *line) {
    int num, min, max;
    int first = 1;
    const char *p = line;

    while (1) {
        char *end;
        num = strtol(p, &end, 10);
        if (p == end) break;   // чисел больше нет
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
        printf("[%d] Строка: %s", getpid(), line);
        printf("  -> минимум: %d, максимум: %d\n", min, max);
        fflush(stdout);
    }
}

int main() {
    srand(time(NULL) ^ getpid());   // уникальный seed

    // 1. Создаём семафор (мьютекс) с начальным значением 1
    sem_t *sem = sem_open(SEM_NAME, O_CREAT, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    // 2. Создаём пустой файл (на всякий случай)
    FILE *f = fopen(FILENAME, "w");
    if (f) fclose(f);

    // 3. Запускаем дочерний процесс
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        sem_close(sem);
        sem_unlink(SEM_NAME);
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        /* ---------- ДОЧЕРНИЙ ПРОЦЕСС (потребитель) ---------- */
        printf("Дочерний процесс (PID = %d) запущен. Ожидание строк...\n", getpid());

        while (1) {
            sem_wait(sem);   // захватываем мьютекс

            FILE *file = fopen(FILENAME, "r+");
            if (!file) {
                perror("fopen (child)");
                sem_post(sem);
                sleep(1);
                continue;
            }

            // Читаем первую строку
            char line[256];
            if (fgets(line, sizeof(line), file) == NULL) {
                // Файл пуст – ничего не делаем
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

            // Перезаписываем файл только остатком (удаляем прочитанную строку)
            file = fopen(FILENAME, "w");
            if (file) {
                fwrite(rest, 1, rest_len, file);
                fclose(file);
            } else {
                perror("fopen write (child)");
            }

            sem_post(sem);   // освобождаем мьютекс

            // Проверяем, не является ли строка маркером завершения
            if (strncmp(line, "END", 3) == 0) {
                printf("Дочерний процесс получил сигнал завершения. Выход.\n");
                break;
            }

            // Обрабатываем строку (ищем min/max)
            process_line(line);
        }

        sem_close(sem);
        exit(EXIT_SUCCESS);
    } 
    else {
        /* ---------- РОДИТЕЛЬСКИЙ ПРОЦЕСС (производитель) ---------- */
        printf("Родительский процесс (PID = %d) генерирует данные.\n", getpid());

        for (int i = 0; i < LINES_COUNT; ++i) {
            char line[256];
            generate_line(line, sizeof(line));

            sem_wait(sem);   // захватываем файл

            FILE *file = fopen(FILENAME, "a");  // добавление в конец
            if (file) {
                fputs(line, file);
                fclose(file);
                printf("Родитель записал: %s", line);
            } else {
                perror("fopen (parent)");
            }

            sem_post(sem);   // освобождаем

            sleep(rand() % 2 + 1);  // небольшая пауза
        }

        // Посылаем дочернему процессу маркер завершения
        sem_wait(sem);
        FILE *file = fopen(FILENAME, "a");
        if (file) {
            fprintf(file, "END\n");
            fclose(file);
            printf("Родитель записал маркер завершения.\n");
        }
        sem_post(sem);

        // Ждём завершения дочернего процесса
        wait(NULL);
        printf("Дочерний процесс завершён. Родитель закрывает семафор.\n");

        // Удаляем семафор
        sem_close(sem);
        sem_unlink(SEM_NAME);
    }

    return 0;
}
