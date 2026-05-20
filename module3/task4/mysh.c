#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_CMD_LEN 4096
#define MAX_ARGS 128
#define MAX_CMDS 64

// Проверяет, существует ли файл и является ли исполняемым
int is_executable(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && (st.st_mode & S_IXUSR));
}

// Разбор строки команды на аргументы, с выделением файлов перенаправления
// Возвращает количество аргументов; input_file и output_file заполняются, если есть '<' или '>'
int parse_command(char *cmd, char **argv, char **input_file, char **output_file) {
    int argc = 0;
    char *token;
    *input_file = NULL;
    *output_file = NULL;

    token = strtok(cmd, " \t\n");
    while (token != NULL && argc < MAX_ARGS - 1) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) *input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) *output_file = token;
        } else {
            argv[argc++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;
    return argc;
}

// Запуск одной команды с заданными дескрипторами ввода/вывода и/или файлами
void run_command(char **argv, int argc, int input_fd, int output_fd,
                 char *in_file, char *out_file) {
    // Перенаправление ввода
    if (in_file) {
        int fd = open(in_file, O_RDONLY);
        if (fd < 0) { perror("open input"); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    } else if (input_fd != STDIN_FILENO) {
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);
    }
    // Перенаправление вывода
    if (out_file) {
        int fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) { perror("open output"); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    } else if (output_fd != STDOUT_FILENO) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }
    // Поиск программы
    char local_path[1024];
    snprintf(local_path, sizeof(local_path), "./%s", argv[0]);
    if (is_executable(local_path))
        execv(local_path, argv);
    else
        execvp(argv[0], argv);
    perror("exec");
    exit(1);
}

int main() {
    char input[MAX_CMD_LEN];
    char *commands[MAX_CMDS][MAX_ARGS];
    char *in_files[MAX_CMDS] = {NULL};
    char *out_files[MAX_CMDS] = {NULL};
    int argcs[MAX_CMDS];
    int cmd_count;

    while (1) {
        printf("mysh> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) {
            printf("\nВыход.\n");
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0) continue;
        if (strcmp(input, "exit") == 0) break;

        // Разбиваем на команды по символу '|'
        cmd_count = 0;
        char *saveptr;
        char *cmd_part = strtok_r(input, "|", &saveptr);
        while (cmd_part && cmd_count < MAX_CMDS) {
            // Удаляем пробелы по краям
            while (*cmd_part == ' ') cmd_part++;
            char *end = cmd_part + strlen(cmd_part) - 1;
            while (end > cmd_part && *end == ' ') end--;
            *(end+1) = '\0';

            char cmd_copy[MAX_CMD_LEN];
            strcpy(cmd_copy, cmd_part);
            argcs[cmd_count] = parse_command(cmd_copy, commands[cmd_count],
                                             &in_files[cmd_count], &out_files[cmd_count]);
            cmd_count++;
            cmd_part = strtok_r(NULL, "|", &saveptr);
        }

        if (cmd_count == 0) continue;

        int prev_fd = STDIN_FILENO;
        for (int i = 0; i < cmd_count; i++) {
            int pipefd[2];
            int next_fd = STDOUT_FILENO;
            if (i < cmd_count - 1) {
                if (pipe(pipefd) < 0) { perror("pipe"); break; }
                next_fd = pipefd[1];
            }

            pid_t pid = fork();
            if (pid == 0) {
                // Дочерний процесс
                if (i < cmd_count - 1) close(pipefd[0]);
                run_command(commands[i], argcs[i],
                            (i == 0 && in_files[i]) ? -1 : prev_fd,
                            (i == cmd_count-1 && out_files[i]) ? -1 : next_fd,
                            (i == 0) ? in_files[i] : NULL,
                            (i == cmd_count-1) ? out_files[i] : NULL);
            } else if (pid > 0) {
                // Родитель
                if (prev_fd != STDIN_FILENO) close(prev_fd);
                if (i < cmd_count - 1) close(pipefd[1]);
                prev_fd = (i < cmd_count - 1) ? pipefd[0] : STDIN_FILENO;
            } else {
                perror("fork");
            }
        }
        // Ждём завершения всех дочерних процессов
        while (wait(NULL) > 0);
    }
    return 0;
}
