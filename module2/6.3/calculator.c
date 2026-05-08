#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Структура для хранения информации об операции
typedef struct {
    char *name;                     // название операции (из имени dll)
    double (*func)(double, double); // указатель на функцию
    HMODULE handle;                 // дескриптор библиотеки
} Operation;

// Удаляет расширение .dll (регистронезависимо)
char *get_base_name(const char *filename) {
    char *name = _strdup(filename);
    if (!name) return NULL;
    char *p = strstr(name, ".dll");
    if (p) *p = '\0';
    return name;
}

// Загружает все операции из каталога
Operation* load_operations(const char *dir_path, int *op_count) {
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*.dll", dir_path);

    WIN32_FIND_DATA find_data;
    HANDLE hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("В каталоге %s не найдено DLL\n", dir_path);
        return NULL;
    }

    Operation *ops = NULL;
    int count = 0;

    do {
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);

        // Загружаем DLL
        HMODULE hLib = LoadLibrary(full_path);
        if (!hLib) {
            printf("Не удалось загрузить %s (ошибка %lu)\n", find_data.cFileName, GetLastError());
            continue;
        }

        // Получаем адрес функции operation
        double (*func)(double, double) = (double (*)(double, double)) GetProcAddress(hLib, "operation");
        if (!func) {
            printf("В %s не найдена функция 'operation'\n", find_data.cFileName);
            FreeLibrary(hLib);
            continue;
        }

        // Формируем имя операции
        char *op_name = get_base_name(find_data.cFileName);
        if (!op_name) {
            FreeLibrary(hLib);
            continue;
        }

        // Добавляем в массив
        Operation *new_ops = realloc(ops, sizeof(Operation) * (count + 1));
        if (!new_ops) {
            printf("Ошибка памяти\n");
            free(op_name);
            FreeLibrary(hLib);
            break;
        }
        ops = new_ops;
        ops[count].name = op_name;
        ops[count].func = func;
        ops[count].handle = hLib;
        count++;
    } while (FindNextFile(hFind, &find_data));

    FindClose(hFind);
    *op_count = count;
    return ops;
}

int main(int argc, char *argv[]) {
    // Каталог с библиотеками: первый аргумент или текущий каталог
    const char *lib_dir = (argc > 1) ? argv[1] : ".";
    int op_count = 0;
    Operation *operations = load_operations(lib_dir, &op_count);

    if (op_count == 0) {
        printf("Не найдено операций. Создайте DLL с функцией 'operation'.\n");
        return 1;
    }

    double a, b;
    int choice;

    printf("Введите два числа: ");
    if (scanf("%lf %lf", &a, &b) != 2) {
        printf("Ошибка ввода чисел\n");
        return 1;
    }

    printf("\nВыберите операцию:\n");
    for (int i = 0; i < op_count; i++) {
        printf("%d. %s\n", i + 1, operations[i].name);
    }
    printf("Ваш выбор: ");
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > op_count) {
        printf("Неверный выбор!\n");
        return 1;
    }

    double result = operations[choice - 1].func(a, b);
    printf("Результат: %.2f\n", result);

    // Освобождаем ресурсы
    for (int i = 0; i < op_count; i++) {
        FreeLibrary(operations[i].handle);
        free(operations[i].name);
    }
    free(operations);

    return 0;
}