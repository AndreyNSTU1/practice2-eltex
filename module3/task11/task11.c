#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_NUMBERS 50
#define MAX_VALUE   1000
#define SHM_NAME     "/shm_task11"
#define SEM_READY_CHILD "/sem_task11_child"
#define SEM_READY_PARENT "/sem_task11_parent"

typedef struct {
    int count;                  // количество чисел в текущем наборе
    int numbers[MAX_NUMBERS];
    int min_result;
    int max_result;
    int processed_sets;         // общий счётчик обработанных наборов
} shared_data_t;

int shm_fd;
shared_data_t *shared = NULL;
sem_t *sem_child = NULL;    // семафор "родитель->ребёнок"
sem_t *sem_parent = NULL;   // семафор "ребёнок->родитель"
pid_t child_pid = 0;

void cleanup(void) {
    if (shared != NULL && shared != MAP_FAILED) {
        munmap(shared, sizeof(shared_data_t));
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_unlink(SHM_NAME);
    }
    if (sem_child != SEM_FAILED) sem_close(sem_child);
    if (sem_parent != SEM_FAILED) sem_close(sem_parent);
    sem_unlink(SEM_READY_CHILD);
    sem_unlink(SEM_READY_PARENT);
}

void sigint_handler(int sig) {
    printf("\nПолучен SIGINT. Обработано наборов: %d\n", shared->processed_sets);
    if (child_pid > 0) kill(child_pid, SIGTERM);
    cleanup();
    exit(0);
}

int main() {
    srand(time(NULL) ^ getpid());

    // Создаём разделяемую память POSIX
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        perror("ftruncate");
        cleanup();
        exit(1);
    }
    shared = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        cleanup();
        exit(1);
    }
    memset(shared, 0, sizeof(shared_data_t));

    // Создаём семафоры (оба начинаются с 0)
    sem_child = sem_open(SEM_READY_CHILD, O_CREAT, 0666, 0);
    sem_parent = sem_open(SEM_READY_PARENT, O_CREAT, 0666, 0);
    if (sem_child == SEM_FAILED || sem_parent == SEM_FAILED) {
        perror("sem_open");
        cleanup();
        exit(1);
    }

    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        cleanup();
        exit(1);
    }

    if (child_pid == 0) {
        // ---------- Дочерний процесс ----------
        printf("Дочерний PID=%d запущен. Обработка данных...\n", getpid());
        while (1) {
            sem_wait(sem_child);   // ждём данные
            // обработка
            int min_val = shared->numbers[0];
            int max_val = shared->numbers[0];
            for (int i = 1; i < shared->count; i++) {
                if (shared->numbers[i] < min_val) min_val = shared->numbers[i];
                if (shared->numbers[i] > max_val) max_val = shared->numbers[i];
            }
            shared->min_result = min_val;
            shared->max_result = max_val;
            shared->processed_sets++;
            sem_post(sem_parent);  // результат готов
        }
        exit(0);
    }
    else {
        // ---------- Родительский процесс ----------
        signal(SIGINT, sigint_handler);
        printf("Родитель PID=%d. Генерируем наборы. Ctrl+C для завершения.\n", getpid());

        while (1) {
            // Генерация случайного набора
            int count = rand() % MAX_NUMBERS + 1;
            shared->count = count;
            for (int i = 0; i < count; i++) {
                shared->numbers[i] = rand() % (MAX_VALUE + 1);
            }
            // Отправляем данные дочернему
            sem_post(sem_child);
            // Ждём результат
            sem_wait(sem_parent);
            // Выводим
            printf("Набор %d: ", shared->processed_sets);
            for (int i = 0; i < count; i++) printf("%d ", shared->numbers[i]);
            printf("-> мин=%d, макс=%d\n", shared->min_result, shared->max_result);
            usleep(500000); // задержка для наглядности
        }
    }
    return 0;
}
