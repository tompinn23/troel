#ifndef tr_table_h
#define tr_table_h

#include "tr_value.h"
#include <stdbool.h>

#define TABLE_MAX_LOAD 0.7

struct tr_tbl_entry {
  struct tr_string *key;
  struct tr_value value;
};

struct tr_table {
  int count;
  int capacity;
  struct tr_tbl_entry *entries;
};

void tr_table_init(struct tr_table *t);
void tr_table_free(struct tr_table *t);
bool tr_table_insert(struct tr_table *t, struct tr_string *s,
                     struct tr_value val);
bool tr_table_get(struct tr_table *t, struct tr_string *s, struct tr_value *v);
bool tr_table_delete(struct tr_table *t, struct tr_string *s);

#endif // tr_table_h