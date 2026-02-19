#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

int main() {
    uint32_t number = 4;
    uint32_t shifter = 0;
    printf("Enter a number for shifter :");
    if (scanf("%d", &shifter) != 1)
        return EXIT_FAILURE;
    printf("%d\n", number << shifter);
    return EXIT_SUCCESS;
}