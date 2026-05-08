#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <string.h>

int is_number(const char *str) {
    char *endptr;
    strtod(str, &endptr);
    return (endptr != str && *endptr == '\0');
}

void process_argument(const char *arg) {
    printf("[%d] %s -> ", getpid(), arg);
    if (is_number(arg)) {
        double val = strtod(arg, NULL);
        printf("%g %g\n", val, val * 2.0);
    } else {
        printf("%s\n", arg);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <arg1> [arg2 ...]\n", argv[0]);
        return 1;
    }
    int n = argc - 1;
    int mid = n / 2;
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        for (int i = mid + 1; i <= n; i++) process_argument(argv[i]);
        exit(0);
    } else {
        for (int i = 1; i <= mid; i++) process_argument(argv[i]);
        wait(NULL);
    }
    return 0;
}
