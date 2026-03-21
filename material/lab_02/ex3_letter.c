#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define BASE_OFFSET      0x0
#define BASE_LENGTH      4096
#define LEDS_OFFSET      0x0
#define HEX0_OFFSET      0x20
#define HEX4_OFFSET      0X30
#define KEYS_DATA_OFFSET 0x50
#define KEYS_MASK_OFFSET 0x58
#define KEYS_EDGE_OFFSET 0x5C

#define KEY0_MASK       0b0001
#define KEY1_MASK       0b0010
#define KEY2_MASK       0b0100
#define KEY3_MASK       0b1000

// turned to 1 to stop main loop
int loop_stopped = 0;

// main memory cursor
volatile uint8_t* memory_cursor;

// table to remember which letter is on which 7 segment
char hex_letters[6] = {' ', ' ', ' ', ' ', ' ', ' '};
uint8_t current_hex = 0;

/**
 * @brief letters writen using hexadecimal for 7 segments
 */
static const uint8_t letters[26] = {
    /*A*/ 0x77, /*B*/ 0x7C, /*C*/ 0x39, /*D*/ 0x5E, /*E*/ 0x79, /*F*/ 0x71,
    /*G*/ 0x3D, /*H*/ 0x74, /*I*/ 0x06, /*J*/ 0x1E, /*K*/ 0x70, /*L*/ 0x38,
    /*M*/ 0x15, /*N*/ 0x37, /*O*/ 0x3F, /*P*/ 0x73, /*Q*/ 0x67, /*R*/ 0x31,
    /*S*/ 0x6D, /*T*/ 0x78, /*U*/ 0x3E, /*V*/ 0x1C, /*W*/ 0x2A, /*X*/ 0x76,
    /*Y*/ 0x6E, /*Z*/ 0x5B
};

/**
 * @brief answers to signals of type SIGINT to stop the main loop of the program
 * @param sig signal received (not used)
 * @bug it only registers SIGINT once a button is pressed, a fix could be done but a bit lazy
 */
void signal_handler(const int sig) {
    (void) sig; // remove unused parameter
    loop_stopped = 1;
}

/**
 * @brief writes all letters in hex_letters on the 7 segments
 *        this is required else errors appear for unknown reason when writing single letters only
 */
void write_letters() {
    for (int i = 0 ; i < 6 ; ++i) {
        uint8_t hex_actual_position = (i >= 4 ? HEX4_OFFSET + i - 4 : HEX0_OFFSET + i);
        volatile uint8_t *letter = (volatile uint8_t *)(memory_cursor + hex_actual_position);
        *letter = (hex_letters[i] == ' ' ? 0x0 : letters[hex_letters[i] - 'A']); // write over only the hexadecimal we need
    }
}

/**
 * @brief writes the index of the 7 segment from current_hex to the leds from binary to linear format
 */
void write_leds() {
    volatile uint32_t* leds = (volatile uint32_t *)(memory_cursor + LEDS_OFFSET);
    *leds = 1 << current_hex; // write over all the memory
}

/**
 * @brief changes the letter in hex_letters indexed by current_hex to the next one in the alphabet or A after ' '
 */
void next_letter() {
    char *c = &hex_letters[current_hex];
    switch (*c) {
        case ' ': *c = 'A'; break;
        case 'Z': break; // doesn't change the letter
        default: *c = (char) (*c + 1);
    }
}

/**
 * @brief changes the letter in hex_letters indexed by current_hex to the previous one in the alphabet or ' ' before A
 */
void prev_letter() {
    char *c = &hex_letters[current_hex];
    switch (*c) {
        case ' ': // fallthrough
        case 'A': *c = ' '; break;
        default: *c = (char) (*c - 1);
    }
}

/**
 * @brief moves the cursor (current_hex) to the 7 segment to its right
 */
void move_cursor_right() {
    current_hex = (current_hex + 5) % 6;
}

/**
 * @brief moves the cursor (current_hex) to the 7 segment to its left
 */
void move_cursor_left() {
    current_hex = (current_hex + 1) % 6;
}

/**
 * @brief reads keys data to retrieve keys state (useless here cause we only use interrupts to know we clicked)
 * @return int32 value of data
 */
uint32_t read_keys_data() {
    return *((volatile uint32_t *)(memory_cursor + KEYS_DATA_OFFSET));
}

/**
 * @brief reads which keys made an irq in the edge memory
 * @return int32 value of edge
 */
uint32_t read_keys_edge() {
    return *((volatile uint32_t *)(memory_cursor + KEYS_EDGE_OFFSET));
}

/**
 * @brief writes in the edge memory to clear interrupts
 * @param value to write
 */
void clear_keys_edge(const uint32_t value) {
    *((volatile uint32_t *)(memory_cursor + KEYS_EDGE_OFFSET)) = value;
}

/**
 * @brief writes the mask to enable key interrupts
 * @param mask to write
 */
void enable_key_interrupts(const uint32_t mask) {
    *((volatile uint32_t *)(memory_cursor + KEYS_MASK_OFFSET)) = mask;
}

/**
 * @brief changes values for 7 segments and leds to 0x0 to clear them when finishing the program
 */
void clean_hardware() {
    *(uint32_t *)(memory_cursor + HEX0_OFFSET) = 0x0;
    *(uint32_t *)(memory_cursor + HEX4_OFFSET) = 0x0;
    *(uint32_t *)(memory_cursor + LEDS_OFFSET) = 0x0;
}

int main(void) {

    // handle SIGINT
    signal(SIGINT, signal_handler);

    // open file descriptor
    char *file = "/dev/uio0";
    int fd = open(file, O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/uio0\n");
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

    // write original letters and leds
    write_letters();
    write_leds();

    // clear any past interrupts
    clear_keys_edge(0xF);

    // activate interrupts
    enable_key_interrupts(0xF);

    // keep looping until SIGINT
    while (!loop_stopped) {

        uint32_t irq_count = 0;
        int one = 1;

        // reactivate interrupts for uio
        ssize_t nb = write(fd, &one, sizeof(one));
        if (nb != (ssize_t)sizeof(irq_count)) {
            perror("Couldn't write in uio for interrupts\n");
            goto error;
        }

        // wait for an interrupt from uio
        nb = read(fd, &irq_count, sizeof(irq_count));
        if (nb != (ssize_t)sizeof(irq_count)) {
            perror("Couldn't read in uio for interrupts\n");
            goto error;
        }

        // read interrupts
        const uint32_t pending_irq = read_keys_edge();

        // act depending on what interruption happened
        if (pending_irq & KEY0_MASK) {           // if key0, go back one letter
            prev_letter();
            write_letters();
        } else if (pending_irq & KEY1_MASK) {    // if key1, go forward one letter
            next_letter();
            write_letters();
        } else if (pending_irq & KEY2_MASK) {    // if key2, move right for edition
            move_cursor_right();
            write_leds();
        } else if (pending_irq & KEY3_MASK) {    // if key3, move right for edition
            move_cursor_left();
            write_leds();
        }

        // clear interrupts
        clear_keys_edge(pending_irq);
    }

    // clean hardware and close file descriptor
    clean_hardware();
    munmap(map, BASE_LENGTH);
    close(fd);

    return EXIT_SUCCESS;

    // in case a problem occurs once inside main loop
    error:
    clean_hardware();
    munmap(map, BASE_LENGTH);
    close(fd);
    return EXIT_FAILURE;
}