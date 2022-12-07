#include "tr_value.h"

#include "memory.h"
#include <string.h>

extern inline bool tr_value_is_falsey(struct tr_value v);
extern inline bool tr_value_eq(struct tr_value a, struct tr_value b);

void tr_string_cpy(struct tr_string* s, const char* str) {
  s->str = mem_strdup(str);
  tr_string_hash(s);
}

void tr_string_ncpy(struct tr_string* s, const char* str, int len) {
  s->str = mem_strndup(str, len);
  tr_string_hash(s);
}

void tr_string_new(struct tr_string* s, int capacity) {
  s->str = mem_realloc(NULL, 0, sizeof(char) * capacity);
  memset(s->str, 0, sizeof(char) * capacity);
  s->hash = 0;
}

void tr_string_free(struct tr_string* s) { mem_free(s->str); }

uint32_t tr_string__hash(const char* key, uint32_t len, uint32_t seed) {
  uint32_t c1            = 0xcc9e2d51;
  uint32_t c2            = 0x1b873593;
  uint32_t r1            = 15;
  uint32_t r2            = 13;
  uint32_t m             = 5;
  uint32_t n             = 0xe6546b64;
  uint32_t h             = 0;
  uint32_t k             = 0;
  uint8_t* d             = (uint8_t*)key; // 32 bit extract from `key'
  const uint32_t* chunks = NULL;
  const uint8_t* tail    = NULL; // tail - last 8 bytes
  int i                  = 0;
  int l                  = len / 4; // chunk length

  h = seed;

  chunks = (const uint32_t*)(d + l * 4); // body
  tail   = (const uint8_t*)(d + l * 4);  // last 8 byte chunk of `key'

  // for each 4 byte chunk of `key'
  for (i = -l; i != 0; ++i) {
    // next 4 byte chunk of `key'
    k = chunks[i];

    // encode next 4 byte chunk of `key'
    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;

    // append to hash
    h ^= k;
    h = (h << r2) | (h >> (32 - r2));
    h = h * m + n;
  }

  k = 0;

  // remainder
  switch (len & 3) { // `len % 4'
  case 3:
    k ^= (tail[2] << 16);
  case 2:
    k ^= (tail[1] << 8);

  case 1:
    k ^= tail[0];
    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;
    h ^= k;
  }

  h ^= len;

  h ^= (h >> 16);
  h *= 0x85ebca6b;
  h ^= (h >> 13);
  h *= 0xc2b2ae35;
  h ^= (h >> 16);

  return h;
}

void tr_string_hash(struct tr_string* s) { s->hash = tr_string__hash(s->str, strlen(s->str), 0); }
