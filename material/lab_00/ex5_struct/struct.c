#include <stdio.h>
#include <stdlib.h>

// struct named
struct First {
    int i;
    char c;
};

// struct anonym with second as a variable
struct {
    int i;
    char c;
} second;

int main() {

    printf("First size = %ld vs Second size = %ld\n", sizeof(struct First), sizeof(second));

    struct First f = {10, 'a'};
    second.i = 20;
    second.c = 'b';

    printf("First : %d, %c\n", f.i, f.c);
    printf("Second : %d, %c\n", second.i, second.c);

    return EXIT_SUCCESS;
}