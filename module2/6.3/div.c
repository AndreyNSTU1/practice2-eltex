#include <windows.h>
#include <stdio.h>

__declspec(dllexport) double operation(double a, double b) {
    if (b == 0) {
        fprintf(stderr, "Ошибка: деление на ноль!\n");
        return 0.0;
    }
    return a / b;
}