#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64

// Проверяет, существует ли файл и является ли исполняемым
int is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && (st.st_mode & S_IXUSR)) {
        return 1;
    }
    return 0;
}

// Разбивает строку на аргументы
int parse_command(char *cmd, char **argv) {
    int argc = 0;
    char *token = strtok(cmd, " \t\n");
    while (token != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;
    return argc;
}

int main() {
    char input[MAX_CMD_LEN];
    char *argv[MAX_ARGS];
    int status;

    while (1) {
        printf("mysh> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\nВыход.\n");
            break;
        }

        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0) continue;
        if (strcmp(input, "exit") == 0) break;

        int argc = parse_command(input, argv);
        if (argc == 0) continue;

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) {
            // Сначала проверяем текущую директорию
            char local_path[MAX_CMD_LEN];
            snprintf(local_path, sizeof(local_path), "./%s", argv[0]);
            if (is_executable(local_path)) {
                // Заменяем первый аргумент на путь с ./
                argv[0] = local_path;
                execv(local_path, argv);
                perror("execv");
                exit(EXIT_FAILURE);
            }
            // Если в текущей директории нет, пробуем через PATH
            execvp(argv[0], argv);
            fprintf(stderr, "Ошибка: программа '%s' не найдена\n", argv[0]);
            exit(EXIT_FAILURE);
        } else {
            waitpid(pid, &status, 0);
        }
    }
    return 0;
}
