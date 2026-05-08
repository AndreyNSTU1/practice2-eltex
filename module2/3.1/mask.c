#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define stat _stat
#endif

// вывод в битах
void print_binary(mode_t mode) {
    for (int i = 8; i >= 0; i--) {
        printf("%d", (mode >> i) & 1);
    }
    printf("\n");
}

// вывод в rwx
void print_symbolic(mode_t mode) {
    char perms[] = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) {
        if (mode & (1 << (8 - i)))
            printf("%c", perms[i]);
        else
            printf("-");
    }
    printf("\n");
}

// строка rwxr-xr-x → число
mode_t parse_symbolic(char *s) {
    mode_t mode = 0;
    for (int i = 0; i < 9; i++) {
        if (s[i] != '-')
            mode |= (1 << (8 - i));
    }
    return mode;
}

// вывод всего
void print_all(mode_t mode) {
    printf("\nСимвольное: ");
    print_symbolic(mode);

    printf("Цифровое: %o\n", mode);

    printf("Битовое: ");
    print_binary(mode);
}

// изменение прав (u+x, g-w, o=r)
mode_t modify(mode_t mode, char *cmd) {
    char who = cmd[0];
    char op = cmd[1];
    char perm = cmd[2];

    int shift = (who == 'u') ? 6 : (who == 'g') ? 3 : 0;

    int val = (perm == 'r') ? 4 : (perm == 'w') ? 2 : 1;

    if (op == '+')
        mode |= (val << shift);
    else if (op == '-')
        mode &= ~(val << shift);
    else if (op == '=')
        mode = (mode & ~(7 << shift)) | (val << shift);

    return mode;
}

int main() {
    int choice;
    char input[50];
    mode_t mode = 0;

    printf("1 - Ввести права\n");
    printf("2 - Права файла\n");
    printf("3 - Изменить права\n");
    printf("Выбор: ");
    scanf("%d", &choice);

    // --- 1 ---
    if (choice == 1) {
        printf("Введите права (например 755 или rwxr-xr-x): ");
        scanf("%s", input);

        if (strlen(input) == 3)
            mode = strtol(input, NULL, 8);
        else
            mode = parse_symbolic(input);

        print_all(mode);
    }

    // --- 2 ---
    else if (choice == 2) {
        struct stat st;

        printf("Введите имя файла: ");
        scanf("%s", input);

        if (stat(input, &st) != 0) {
            printf("Ошибка чтения файла\n");
            return 1;
        }

        mode = st.st_mode & 0777;

        print_all(mode);

        printf("\nСравни с командой: ls -l %s\n", input);
    }

    // --- 3 ---
    else if (choice == 3) {
        printf("Введите начальные права (например 755): ");
        scanf("%s", input);

        mode = strtol(input, NULL, 8);

        printf("Введите команду (например u+x): ");
        scanf("%s", input);

        mode = modify(mode, input);

        print_all(mode);
    }

    return 0;
}