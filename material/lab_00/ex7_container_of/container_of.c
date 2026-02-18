#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#define container_of(ptr, type, member) \
    (type*)(void*)((char*)ptr - offsetof(type, member))

struct /*__attribute__((packed))*/ Car {
    short nb_wheels;
    double length;
    char name[50];
};

int main (int argc, char* argv[]) {

    struct Car car = {4, 6.25, "Toyota Corolla"};

    printf("Car (%p) created successfully! Sizeof(struct Car) = %ld\n\n", (void *)&car, sizeof(struct Car));

    printf("Car container of nb_wheels (%p) : %s\n", (void *)&(car.nb_wheels),
        container_of(&(car.nb_wheels), struct Car, nb_wheels) == &car ? "true" : "false");
    printf("Registered offset %ld\n\n", offsetof(struct Car, nb_wheels));

    printf("Car container of length (%p) : %s\n", (void *)&(car.length),
        container_of(&(car.length), struct Car, length) == &car ? "true" : "false");
    printf("Registered offset %ld\n\n", offsetof(struct Car, length));

    printf("Car container of name (%p) : %s\n", (void *)&(car.name),
        container_of(&(car.name), struct Car, name) == &car ? "true" : "false");
    printf("Registered offset %ld\n\n", offsetof(struct Car, name));

    return EXIT_SUCCESS;
}