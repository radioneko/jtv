#ifndef JTV_MEMORY_H
#define JTV_MEMORY_H

void* xcalloc(unsigned nmemb, unsigned size);
void* xmalloc(unsigned size);
void* xrealloc(void *ptr, unsigned size);
char* xstrdup(const char *str);

#endif
