#include <stdio.h>
#include <limits.h>

#ifndef ENUM_GEN
#define EXPORT_TABLE
#endif

#include "test2.c"

#ifndef ENUM_GEN
#include "lists/names.c"
#endif

int
main (void)
{
	int i;
	for (i = 0; names[i].key; ++i)
	{
		printf("%s\t%d\n", names[i].key, names[i].value);
	}
	return 0;
}
