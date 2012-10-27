#include "jtv_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void*
xcalloc(unsigned nmemb, unsigned size)
{
	void *result = calloc(nmemb, size);
	if (!result) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	return result;
}

void*
xmalloc(unsigned size)
{
	void *result = malloc(size);
	if (!result) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	return result;
}

void*
xrealloc(void *ptr, unsigned size)
{
	void *result = realloc(ptr, size);
	if (!result) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	return result;
}

char*
xstrdup(const char *str)
{
	char *result = strdup(str);
	if (!result) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	return result;
}

