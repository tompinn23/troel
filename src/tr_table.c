#include "tr_table.h"

#include "memory.h"
#include "tr_value.h"

#include <stdlib.h>
#include <string.h>
void tr_table_init(struct tr_table* t) {
  t->count    = 0;
  t->capacity = 0;
  t->entries  = NULL;
}

void tr_table_free(struct tr_table* t) {
  mem_free(t->entries);
  tr_table_init(t);
}

static struct tr_tbl_entry* tr_table_find_entry(struct tr_tbl_entry* entries, int capacity,
                                                struct tr_string* s) {
  uint32_t index                 = s->hash % capacity;
  struct tr_tbl_entry* tombstone = NULL;
  for (;;) {
    struct tr_tbl_entry* entry = &entries[index];
    if (entry->key == NULL) {
      if (entry->value.type == VAL_NIL) {
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL)
          tombstone = entry;
      }
    } else if (strcmp(entry->key->str, s->str) == 0) {
      return entry;
    }
    index = (index + 1) % capacity;
  }
}

static void table_adjust_capacity(struct tr_table* t, int cap) {
  struct tr_tbl_entry* entries = mem_realloc(NULL, 0, cap * sizeof(struct tr_tbl_entry));
  for (int i = 0; i < cap; i++) {
    entries[i].key   = NULL;
    entries[i].value = NIL_VAL;
  }

  t->count = 0;
  for (int i = 0; i < t->capacity; i++) {
    struct tr_tbl_entry* entry = &t->entries[i];
    if (entry->key == NULL)
      continue;

    struct tr_tbl_entry* dest = tr_table_find_entry(entries, cap, entry->key);

    dest->key   = entry->key;
    dest->value = entry->value;
    t->count++;
  }
  mem_free(t->entries);
  t->entries  = entries;
  t->capacity = cap;
}

void tr_table_insert_all(struct tr_table* from, struct tr_table* to) {
  for (int i = 0; i < from->capacity; i++) {
    struct tr_tbl_entry* entry = &from->entries[i];
    if (entry->key != NULL) {
      tr_table_insert(to, entry->key, entry->value);
    }
  }
}

bool tr_table_insert(struct tr_table* t, struct tr_string* s, struct tr_value val) {
  if (t->count + 1 > t->capacity * TABLE_MAX_LOAD) {
    int cap = t->capacity * 2 + 8;
    table_adjust_capacity(t, cap);
    t->capacity = cap;
  }
  struct tr_tbl_entry* entry = tr_table_find_entry(t->entries, t->capacity, s);
  bool newKey                = entry->key == NULL;
  if (newKey && entry->value.type == VAL_NIL)
    t->count++;
  entry->key   = tr_string_new_cpy(s);
  entry->value = val;
  return newKey;
}

bool tr_table_get(struct tr_table* t, struct tr_string* s, struct tr_value* v) {
  if (t->count == 0)
    return false;
  struct tr_tbl_entry* entry = tr_table_find_entry(t->entries, t->capacity, s);
  if (entry->key == NULL)
    return false;
  *v = entry->value;
  return true;
}

bool tr_table_delete(struct tr_table* t, struct tr_string* s) {
  if (t->count == 0)
    return false;
  struct tr_tbl_entry* entry = tr_table_find_entry(t->entries, t->capacity, s);
  if (entry->key == NULL)
    return false;
  tr_string_free(entry->key);
  entry->key   = NULL;
  entry->value = (struct tr_value){.type = VAL_LNG, .l = 0xdeadbeef};
  return true;
}