#ifndef SECOND_H // only include if necessary
#define SECOND_H "second"

#include <stdio.h>

#include "first.h"

int id2 = 2;

void second()
{
	printf("I am %s (id = %d) and I introduce no-one :(\n", SECOND_H, id2);
}

#endif // SECOND_H