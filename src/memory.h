#ifndef tr_memory_h
#define tr_memory_h

#include <stddef.h>

#define mem_free(ptr) mem_realloc(ptr, 0, 0)
#define mem_alloc(size) mem_realloc(NULL, 0, size)

char *mem_strdup(const char *str);
char *mem_strndup(const char *str, int len);

void *mem_realloc(void *old, size_t old_sz, size_t sz);

#endif // tr_memory_h