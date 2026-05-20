#include <stdio.h>
#include <stdlib.h>

int main() {
    int max = 0, num, first = 1;
    while (scanf("%d", &num) == 1) {
        if (first) { max = num; first = 0; }
        else if (num > max) max = num;
    }
    if (first) printf("Нет чисел\n");
    else printf("%d\n", max);
    return 0;
}
