#include <windows.h>

__declspec(dllexport) double operation(double a, double b) {
    return a * b;
}