#include <stdio.h>

// Операции
double add(double a, double b) {
    return a + b;
}

double sub(double a, double b) {
    return a - b;
}

double mul(double a, double b) {
    return a * b;
}

double divide(double a, double b) {
    if (b == 0) {
        printf("Ошибка: деление на 0!\n");
        return 0;
    }
    return a / b;
}

// Тип указателя на функцию
typedef double (*Operation)(double, double);

int main() {
    // Массив указателей на функции
    Operation operations[] = { add, sub, mul, divide };

    char *names[] = {
        "Сложение",
        "Вычитание",
        "Умножение",
        "Деление"
    };

    int size = sizeof(operations) / sizeof(operations[0]);

    double a, b;
    int choice;

    printf("Введите два числа: ");
    scanf("%lf %lf", &a, &b);

    printf("\nВыберите операцию:\n");
    for (int i = 0; i < size; i++) {
        printf("%d. %s\n", i + 1, names[i]);
    }

    printf("Ваш выбор: ");
    scanf("%d", &choice);

    if (choice < 1 || choice > size) {
        printf("Неверный выбор!\n");
        return 1;
    }

    // Вызов функции через указатель
    double result = operations[choice - 1](a, b);

    printf("Результат: %.2f\n", result);

    return 0;
}