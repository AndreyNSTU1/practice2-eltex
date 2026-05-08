#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_NUMBERS 50
#define MAX_VALUE   1000
#define SHM_SIZE    (sizeof(int) * 3 + MAX_NUMBERS * sizeof(int))

struct shared_data {
    int count;                  // количество чисел в текущем наборе
    int numbers[MAX_NUMBERS];   // массив чисел
    int min_result;
    int max_result;
    int processed_sets;         // счётчик обработанных наборов (увеличивает дочерний)
};

int shm_id = -1;
int sem_id = -1;
struct shared_data *shared = NULL;
pid_t child_pid = 0;

void sem_wait(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    semop(sem_id, &op, 1);
}

void sem_signal(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    semop(sem_id, &op, 1);
}

void create_semaphores() {
    sem_id = semget(IPC_PRIVATE, 2, IPC_CREAT | 0666);
    if (sem_id == -1) { perror("semget"); exit(1); }
    semctl(sem_id, 0, SETVAL, 0);
    semctl(sem_id, 1, SETVAL, 0);
}

void cleanup() {
    if (shared && shmdt(shared) == -1) perror("shmdt");
    if (shm_id != -1) shmctl(shm_id, IPC_RMID, NULL);
    if (sem_id != -1) semctl(sem_id, 0, IPC_RMID);
}

void sigint_handler(int sig) {
    printf("\nПолучен SIGINT. Количество обработанных наборов: %d\n", 
           shared ? shared->processed_sets : 0);
    if (child_pid > 0) kill(child_pid, SIGTERM);
    cleanup();
    exit(0);
}

int main() {
    srand(time(NULL) ^ getpid());

    key_t shm_key = ftok(".", 'S');
    if (shm_key == -1) { perror("ftok"); exit(1); }
    shm_id = shmget(shm_key, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) { perror("shmget"); exit(1); }
    shared = (struct shared_data *)shmat(shm_id, NULL, 0);
    if (shared == (void *)-1) { perror("shmat"); exit(1); }
    memset(shared, 0, SHM_SIZE);
    shared->processed_sets = 0;

    create_semaphores();

    child_pid = fork();
    if (child_pid < 0) { perror("fork"); cleanup(); exit(1); }

    if (child_pid == 0) {
        // Дочерний процесс
        printf("Дочерний PID=%d запущен. Ожидание...\n", getpid());
        while (1) {
            sem_wait(sem_id, 0);   // ждём данные от родителя
            if (shared->count == 0) break;  // защита

            int min_val = shared->numbers[0];
            int max_val = shared->numbers[0];
            for (int i = 1; i < shared->count; i++) {
                if (shared->numbers[i] < min_val) min_val = shared->numbers[i];
                if (shared->numbers[i] > max_val) max_val = shared->numbers[i];
            }
            shared->min_result = min_val;
            shared->max_result = max_val;
            shared->processed_sets++;  // увеличиваем счётчик

            sem_signal(sem_id, 1);     // результат готов
        }
        printf("Дочерний завершён.\n");
        exit(0);
    } 
    else {
        // Родительский процесс
        signal(SIGINT, sigint_handler);
        printf("Родитель PID=%d. Ctrl+C для выхода.\n", getpid());

        while (1) {
            int count = rand() % MAX_NUMBERS + 1;
            shared->count = count;
            for (int i = 0; i < count; i++)
                shared->numbers[i] = rand() % (MAX_VALUE + 1);

            sem_signal(sem_id, 0);     // данные готовы
            sem_wait(sem_id, 1);       // ждём результат

            printf("Набор %d: ", shared->processed_sets);
            for (int i = 0; i < count; i++) printf("%d ", shared->numbers[i]);
            printf("-> мин=%d, макс=%d\n", shared->min_result, shared->max_result);

            usleep(500000);
        }
    }
    return 0;
}
