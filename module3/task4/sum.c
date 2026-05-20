#include <stdio.h>
#include <stdlib.h>

int main() {
    int sum = 0, num;
    while (scanf("%d", &num) == 1) {
        sum += num;
    }
    printf("%d\n", sum);
    return 0;
}
