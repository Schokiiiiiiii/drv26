#include <stdlib.h>
#include <stdio.h>

int main() {

    int beefcafe = 0xBEEFCAFE;
    char *ptr = (char *)(&beefcafe);

    printf("0x");
    for (int i = 0 ; i < 4 ; ++i) {
        printf("%X", ((int) (*(ptr + (3 - i)))) & 0xff);
    }

    printf("\n");

    return EXIT_SUCCESS;
}