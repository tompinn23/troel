#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* mem_realloc(void* ptr, size_t old, size_t new) {
  if (ptr == NULL && new == 0)
    return NULL;
  if (new == 0) {
    free(ptr);
    return NULL;
  }
  void* np = realloc(ptr, new);
  if (!np) {
    fprintf(stderr, "Out of memory!");
    exit(EXIT_FAILURE);
  }
  return np;
}

char* mem_strdup(const char* str) {
  size_t len = strlen(str);
  char* new = mem_realloc(NULL, 0, (len + 1) * sizeof(char));
  memcpy(new, str, len);
  new[len] = '\0';
  return new;
}

char* mem_strndup(const char* str, int max) {
  size_t len = strlen(str);
  if(len > max)
    len = max;
  char* new = mem_realloc(NULL, 0, (len + 1) * sizeof(char));
  memcpy(new, str, len);
  new[len] = '\0';
  return new;
}