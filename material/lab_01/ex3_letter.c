#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define BASE_OFFSET     0xFF200000
#define BASE_LENGTH     0x5000
#define LEDS_OFFSET     0X00000000
#define HEX3TO0_OFFSET  0x00000020
#define HEX5TO4_OFFSET  0x00000030
#define KEYS_OFFSET     0x00000050

#define KEY0_MASK       0b0001
#define KEY1_MASK       0b0010

int loop_stopped = 0;    // stops main loop if received signal
uint8_t* memory_cursor;  // aims at the start of the memory

static const uint8_t letters[26] = {
    /*A*/ 0x77, /*B*/ 0x7C, /*C*/ 0x39, /*D*/ 0x5E, /*E*/ 0x79, /*F*/ 0x71,
    /*G*/ 0x3D, /*H*/ 0x74, /*I*/ 0x06, /*J*/ 0x1E, /*K*/ 0x70, /*L*/ 0x38,
    /*M*/ 0x15, /*N*/ 0x37, /*O*/ 0x3F, /*P*/ 0x73, /*Q*/ 0x67, /*R*/ 0x31,
    /*S*/ 0x6D, /*T*/ 0x78, /*U*/ 0x3E, /*V*/ 0x1C, /*W*/ 0x2A, /*X*/ 0x76,
    /*Y*/ 0x6E, /*Z*/ 0x5B
};

void signal_handler(const int sig) {
    (void) sig; // remove unused parameter
    loop_stopped = 1;
}

void write_letter(const char c) {
    uint32_t* hex0 = (uint32_t *)(memory_cursor + HEX3TO0_OFFSET);
    *hex0 = letters[c - 'A']; // write over only the hexadecimal we need
}

void write_leds(const char c) {
    uint32_t* leds = (uint32_t *)(memory_cursor + LEDS_OFFSET);
    *leds = (uint32_t) c; // write over all the memory
}

uint32_t read_keys(void) {
    const uint32_t *keys = (uint32_t *)(memory_cursor + KEYS_OFFSET);
    return *keys;
}

char next_letter(const char c) {
    return (c == 'Z') ? 'A' : (char)(c + 1);
}

char prev_letter(const char c) {
    return (c == 'A') ? 'Z' : (char)(c - 1);
}

void msleep(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void clean_hardware() {
    uint8_t* hex0 = (uint8_t *)(memory_cursor + HEX3TO0_OFFSET);
    *hex0 = 0x0;
    write_leds(0x0);
}

int main(void) {

    // handle SIGINT
    signal(SIGINT, signal_handler);

    // open file descriptor
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("Failed to open /dev/mem\n");
        return EXIT_FAILURE;
    }

    // open a map of the memory at the base address
    void* map = mmap(NULL, BASE_LENGTH,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 fd,
                 BASE_OFFSET);
    if (map == MAP_FAILED) {
        perror("Failed to open mmap\n");
        close(fd);
        return EXIT_FAILURE;
    }

    // link memory to pointer
    memory_cursor = (uint8_t *)map;

    // original letter is A
    char letter = 'A';
    write_letter(letter);
    write_leds(letter);

    // remember previous keys to compare
    uint32_t prev_key = read_keys();

    // keys are active on low
    int prev_key0_pressed = (prev_key & KEY0_MASK) == 0;
    int prev_key1_pressed = (prev_key & KEY1_MASK) == 0;

    // keep looping until SIGINT
    while (!loop_stopped) {

        // read keys
        const uint32_t key = read_keys();
        const int key0_pressed = (key & KEY0_MASK) == 0;
        const int key1_pressed = (key & KEY1_MASK) == 0;

        // if key0, go back one letter
        if (key0_pressed && !prev_key0_pressed) {
            letter = prev_letter(letter);
            write_letter(letter);
            write_leds(letter);
        }

        // if key1, go forward one letter
        if (key1_pressed && !prev_key1_pressed) {
            letter = next_letter(letter);
            write_letter(letter);
            write_leds(letter);
        }

        // remember keys pressed
        prev_key0_pressed = key0_pressed;
        prev_key1_pressed = key1_pressed;

        // let some time inbetween loops
        msleep(10);
    }

    // clean hardware and close file descriptor
    clean_hardware();
    close(fd);

    return EXIT_SUCCESS;
}