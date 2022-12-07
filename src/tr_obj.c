#include "tr_obj.h"

#include <stdlib.h>

void tr_object_init(struct tr_object* obj) {
  obj->type     = OBJ_NULL;
  obj->destruct = NULL;
}

void tr_object_destroy(struct tr_object* obj) {
  if (obj->destruct) {
    obj->destruct(obj);
  }
}
