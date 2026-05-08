#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#define OUTPUT_FILE "output.txt"

volatile sig_atomic_t sigint_count = 0;   // счётчик полученных SIGINT
int fd = -1;                              // дескриптор файла

// Единый обработчик для SIGINT и SIGQUIT
void signal_handler(int signo) {
    const char *msg;
    size_t len;

    if (signo == SIGINT) {
        sigint_count++;
        msg = "Received and handled SIGINT\n";
        len = strlen(msg);
        write(fd, msg, len);

        if (sigint_count >= 3) {
            msg = "Third SIGINT, exiting...\n";
            write(fd, msg, strlen(msg));
            close(fd);
            _exit(0);          // аварийное завершение без вызова atexit
        }
    } else if (signo == SIGQUIT) {
        msg = "Received and handled SIGQUIT\n";
        len = strlen(msg);
        write(fd, msg, len);
    }
}

int main() {
    // Открываем (или создаём) файл для записи, старый содержимое стирается
    fd = open(OUTPUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // Настройка обработчика сигналов 
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // системные вызовы могут прерываться 

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction SIGINT");
        close(fd);
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGQUIT, &sa, NULL) < 0) {
        perror("sigaction SIGQUIT");
        close(fd);
        exit(EXIT_FAILURE);
    }

    int counter = 1;
    char buffer[32];
    while (1) {
        int len = snprintf(buffer, sizeof(buffer), "%d\n", counter);
        write(fd, buffer, len);   // запись числа в файл
        counter++;
        sleep(1);                  // пауза 1 секунда
    }

    //  закрываем файл
    close(fd);
    return 0;
}
