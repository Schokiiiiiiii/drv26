#ifndef FIRST_H // only include if necessary
#define FIRST_H "first"

#include <stdio.h>

#include "second.h"

int id1 = 1;

void first()
{
	printf("Hello! I am %s (id %d) and my colleague is %s\n", FIRST_H, id1, SECOND_H);
}

#endif // FIRST_H