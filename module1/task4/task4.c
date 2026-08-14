#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define SHM_SIZE        4096
#define MAX_NUMBERS     10
#define SLEEP_PRODUCER  2
#define SLEEP_CONSUMER  2

#define IPC_PATH        "."
#define IPC_ID          'P'

typedef struct shm_header {
    int first_block_offset;
    int free_offset;
    int total_size;
    int producer_finished;
} shm_header;

#define BLOCK_HEADER_SIZE   (2 * sizeof(int))
#define BLOCK_SIZE(count)   (BLOCK_HEADER_SIZE + (count) * sizeof(int))
#define BLOCK_PTR(base, offset)   ((int*)((char*)(base) + (offset)))

void sem_wait(int semid) {
    struct sembuf op = {0, -1, 0};
    if (semop(semid, &op, 1) == -1) {
        perror("semop wait");
        exit(EXIT_FAILURE);
    }
}

void sem_signal(int semid) {
    struct sembuf op = {0, 1, 0};
    if (semop(semid, &op, 1) == -1) {
        perror("semop signal");
        exit(EXIT_FAILURE);
    }
}

void remove_ipc_objects(key_t key) {
    int shmid = shmget(key, 0, 0);
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    int semid = semget(key, 0, 0);
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
    }
}

void producer_main(void) {
    key_t key;
    int shmid, semid;
    shm_header *header;
    int *block;
    int last_block_offset = 0;
    int total_blocks = 0;

    if ((key = ftok(IPC_PATH, IPC_ID)) == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    remove_ipc_objects(key);

    shmid = shmget(key, SHM_SIZE, IPC_CREAT | IPC_EXCL | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    semid = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semid == -1) {
        perror("semget");
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }
    if (semctl(semid, 0, SETVAL, 1) == -1) {
        perror("semctl SETVAL");
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }

    header = (shm_header*)shmat(shmid, NULL, 0);
    if (header == (void*)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }

    header->first_block_offset = sizeof(shm_header);
    header->free_offset = sizeof(shm_header);
    header->total_size = SHM_SIZE;
    header->producer_finished = 0;

    srand(time(NULL) ^ getpid());

    printf("Производитель начал генерацию наборов...\n");

    while (1) {
        sem_wait(semid);

        int free_bytes = SHM_SIZE - header->free_offset;
        if (free_bytes < BLOCK_HEADER_SIZE) {
            sem_signal(semid);
            break;
        }

        int count = 1 + rand() % MAX_NUMBERS;
        int block_size = BLOCK_SIZE(count);

        if (header->free_offset + block_size > SHM_SIZE) {
            sem_signal(semid);
            break;
        }

        block = BLOCK_PTR(header, header->free_offset);
        block[0] = count;
        block[1] = 0;

        for (int i = 0; i < count; i++) {
            block[2 + i] = rand() % 100;
        }

        if (last_block_offset != 0) {
            int *prev_block = BLOCK_PTR(header, last_block_offset);
            prev_block[1] = header->free_offset;
        } else {
            header->first_block_offset = header->free_offset;
        }

        last_block_offset = header->free_offset;
        header->free_offset += block_size;
        total_blocks++;

        sem_signal(semid);

        usleep(100000);
    }

    sem_wait(semid);
    header->producer_finished = 1;
    sem_signal(semid);

    printf("Производитель завершил генерацию (всего блоков: %d). Ожидает обработки...\n", total_blocks);

    while (1) {
        int all_done = 1;
        sem_wait(semid);

        int offset = header->first_block_offset;
        while (offset != 0) {
            block = BLOCK_PTR(header, offset);
            if (block[0] != 0) {
                all_done = 0;
                break;
            }
            offset = block[1];
        }

        sem_signal(semid);

        if (all_done) {
            printf("Все блоки обработаны.\n");
            break;
        }

        sleep(SLEEP_PRODUCER);
    }

    shmdt(header);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    printf("Производитель завершил работу.\n");
    exit(EXIT_SUCCESS);
}

void consumer_main(void) {
    key_t key;
    int shmid, semid;
    shm_header *header;
    int *block;

    if ((key = ftok(IPC_PATH, IPC_ID)) == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    shmid = shmget(key, 0, 0);
    if (shmid == -1) {
        perror("shmget: разделяемая память не найдена (запустите сначала производителя)");
        exit(EXIT_FAILURE);
    }

    semid = semget(key, 0, 0);
    if (semid == -1) {
        perror("semget: семафор не найден");
        exit(EXIT_FAILURE);
    }

    header = (shm_header*)shmat(shmid, NULL, 0);
    if (header == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    printf("Потребитель (PID %d) запущен.\n", getpid());

    while (1) {
        int found = 0;

        sem_wait(semid);

        int offset = header->first_block_offset;
        while (offset != 0) {
            block = BLOCK_PTR(header, offset);
            if (block[0] > 0) {
                found = 1;
                break;
            }
            offset = block[1];
        }

        if (found) {
            int count = block[0];
            int nums[MAX_NUMBERS];
            for (int i = 0; i < count; i++) {
                nums[i] = block[2 + i];
            }

            block[0] = 0;

            sem_signal(semid);

            int min = nums[0], max = nums[0];
            for (int i = 1; i < count; i++) {
                if (nums[i] < min) min = nums[i];
                if (nums[i] > max) max = nums[i];
            }

            printf("Потребитель %d: набор из %d чисел, min = %d, max = %d\n",
                   getpid(), count, min, max);

            sleep(SLEEP_CONSUMER);
        } else {
            if (header->producer_finished) {
                sem_signal(semid);
                break;
            } else {
                sem_signal(semid);
                sleep(1);
            }
        }
    }

    shmdt(header);
    printf("Потребитель (PID %d) завершил работу.\n", getpid());
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s producer|consumer\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "producer") == 0) {
        producer_main();
    } else if (strcmp(argv[1], "consumer") == 0) {
        consumer_main();
    } else {
        fprintf(stderr, "Неизвестный аргумент. Используйте producer или consumer.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}
