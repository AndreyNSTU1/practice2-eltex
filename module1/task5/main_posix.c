#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define SHM_SIZE        4096
#define MAX_NUMBERS     10
#define SLEEP_PRODUCER  2
#define SLEEP_CONSUMER  2

#define SHM_NAME        "/shm_pc"
#define SEM_NAME        "/sem_pc"

typedef struct shm_header {
    int first_block_offset;
    int free_offset;
    int total_size;
    int producer_finished;
} shm_header;

#define BLOCK_HEADER_SIZE   (2 * sizeof(int))
#define BLOCK_SIZE(count)   (BLOCK_HEADER_SIZE + (count) * sizeof(int))
#define BLOCK_PTR(base, offset)   ((int*)((char*)(base) + (offset)))

void sem_wait_custom(sem_t *sem) {
    if (sem_wait(sem) == -1) {
        perror("sem_wait");
        exit(EXIT_FAILURE);
    }
}

void sem_signal_custom(sem_t *sem) {
    if (sem_post(sem) == -1) {
        perror("sem_post");
        exit(EXIT_FAILURE);
    }
}

void remove_objects(void) {
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
}

void producer_main(void) {
    int shm_fd;
    shm_header *header;
    int *block;
    int last_block_offset = 0;
    int total_blocks = 0;
    sem_t *sem;

    remove_objects();

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }

    header = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (header == MAP_FAILED) {
        perror("mmap");
        shm_unlink(SHM_NAME);
        close(shm_fd);
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        munmap(header, SHM_SIZE);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }

    header->first_block_offset = sizeof(shm_header);
    header->free_offset = sizeof(shm_header);
    header->total_size = SHM_SIZE;
    header->producer_finished = 0;

    srand(time(NULL) ^ getpid());

    printf("Производитель начал генерацию наборов...\n");

    while (1) {
        sem_wait_custom(sem);

        int free_bytes = SHM_SIZE - header->free_offset;
        if (free_bytes < BLOCK_HEADER_SIZE) {
            sem_signal_custom(sem);
            break;
        }

        int count = 1 + rand() % MAX_NUMBERS;
        int block_size = BLOCK_SIZE(count);

        if (header->free_offset + block_size > SHM_SIZE) {
            sem_signal_custom(sem);
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

        sem_signal_custom(sem);

        usleep(100000);
    }

    sem_wait_custom(sem);
    header->producer_finished = 1;
    sem_signal_custom(sem);

    printf("Производитель завершил генерацию (всего блоков: %d). Ожидает обработки...\n", total_blocks);

    while (1) {
        int all_done = 1;
        sem_wait_custom(sem);

        int offset = header->first_block_offset;
        while (offset != 0) {
            block = BLOCK_PTR(header, offset);
            if (block[0] != 0) {
                all_done = 0;
                break;
            }
            offset = block[1];
        }

        sem_signal_custom(sem);

        if (all_done) {
            printf("Все блоки обработаны.\n");
            break;
        }

        sleep(SLEEP_PRODUCER);
    }

    munmap(header, SHM_SIZE);
    shm_unlink(SHM_NAME);
    sem_close(sem);
    sem_unlink(SEM_NAME);

    printf("Производитель завершил работу.\n");
    exit(EXIT_SUCCESS);
}

void consumer_main(void) {
    int shm_fd;
    shm_header *header;
    int *block;
    sem_t *sem;

    shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open: разделяемая память не найдена (запустите сначала производителя)");
        exit(EXIT_FAILURE);
    }

    header = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (header == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open: семафор не найден");
        munmap(header, SHM_SIZE);
        exit(EXIT_FAILURE);
    }

    printf("Потребитель (PID %d) запущен.\n", getpid());

    while (1) {
        int found = 0;

        sem_wait_custom(sem);

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

            sem_signal_custom(sem);

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
                sem_signal_custom(sem);
                break;
            } else {
                sem_signal_custom(sem);
                sleep(1);
            }
        }
    }

    munmap(header, SHM_SIZE);
    sem_close(sem);
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
