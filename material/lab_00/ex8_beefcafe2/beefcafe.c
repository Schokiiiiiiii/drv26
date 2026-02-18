
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

union Memory {
    uint32_t value;
    uint8_t bytes[4];
};

int main () {

    union Memory beefcafe = {0xBEEFCAFE};

    printf("0x");
    for (int i = 0 ; i < 4 ; ++i) {
        printf("%x", beefcafe.bytes[3 - i] & 0xff);
    }
    printf("\n");

    return EXIT_SUCCESS;
}