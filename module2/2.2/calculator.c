#include <stdio.h>

// функции операций
double add(double a, double b) {
    return a + b;
}

double sub(double a, double b) {
    return a - b;
}

double mul(double a, double b) {
    return a * b;
}

double div(double a, double b) {
    if (b == 0) {
        printf("Ошибка: деление на ноль!\n");
        return 0;
    }
    return a / b;
}

int main() {
    int choice;
    double a, b, result;

    while (1) {
        printf("\nКалькулятор:\n");
        printf("1. Сложение\n");
        printf("2. Вычитание\n");
        printf("3. Умножение\n");
        printf("4. Деление\n");
        printf("0. Выход\n");
        printf("Выберите действие: ");
        
        scanf("%d", &choice);

        if (choice == 0) {
            printf("Выход...\n");
            break;
        }

        printf("Введите два числа: ");
        scanf("%lf %lf", &a, &b);

        switch (choice) {
            case 1:
                result = add(a, b);
                break;
            case 2:
                result = sub(a, b);
                break;
            case 3:
                result = mul(a, b);
                break;
            case 4:
                result = div(a, b);
                break;
            default:
                printf("Неверный выбор!\n");
                continue;
        }

        printf("Результат: %.2lf\n", result);
    }

    return 0;
}