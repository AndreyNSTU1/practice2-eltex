#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Нет аргументов\n");
        return 0;
    }
    int max = atoi(argv[1]);
    for (int i = 2; i < argc; i++) {
        int val = atoi(argv[i]);
        if (val > max) max = val;
    }
    printf("Максимум = %d\n", max);
    return 0;
}
