#ifndef tr_value_h
#define tr_value_h

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum { VAL_NIL, VAL_STR, VAL_LNG, VAL_DBL, VAL_PTR, VAL_BOOL, VAL_OBJ };

struct tr_string {
  char* str;
  uint32_t hash;
};

struct tr_value {
  int type;
  union {
    struct tr_string s;
    bool b;
    long l;
    double d;
    void* p;
    struct tr_object* obj;
  };
};

#define NIL_VAL                                                                                    \
  (struct tr_value) { .type = VAL_NIL }

inline bool tr_value_is_falsey(struct tr_value v) {
  switch (v.type) {
  case VAL_NIL:
    return true;
  case VAL_LNG:
    return v.l == 0;
  case VAL_DBL:
    return v.d == 0;
  case VAL_PTR:
    return v.p == NULL;
  case VAL_BOOL:
    return v.b;
  default:
    return false;
  }
}

inline bool tr_value_eq(struct tr_value a, struct tr_value b) {
  if (a.type != b.type)
    return false;
  switch (a.type) {
  case VAL_NIL:
    return true;
  case VAL_BOOL:
    return a.b == b.b;
  case VAL_LNG:
    return a.l == b.l;
  case VAL_DBL:
    return a.d == b.d;
  case VAL_STR:
    return a.s.hash == b.s.hash;
  default:
    return false;
  }
}

void tr_string_cpy(struct tr_string* s, const char* str);
void tr_string_ncpy(struct tr_string* s, const char* str, int len);
void tr_string_new(struct tr_string* s, int capacity);
void tr_string_free(struct tr_string* s);
void tr_string_hash(struct tr_string* s);
#endif
