#include "tr_obj.h"

#include <stdlib.h>

void tr_object_init(struct tr_object* obj, int type) {
  obj->type     = type;
  obj->destruct = NULL;
}

void tr_object_destroy(struct tr_object* obj) {
  if (obj->destruct) {
    obj->destruct(obj);
  }
}
