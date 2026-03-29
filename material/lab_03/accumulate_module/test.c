#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    int fd;
    const uint64_t val = 3;
    uint64_t result;

    if ((fd = open("/dev/accumulate-0", O_RDWR)) < 0)
        return EXIT_FAILURE;

    printf("Writing 3...\n");
    write(fd, &val, sizeof(val));
    printf("Reading number inside driver...\n");
    read(fd, &result, sizeof(result));

    printf("Result: %llu\n", result);

    close(fd);
    return EXIT_SUCCESS;
}