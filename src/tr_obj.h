#ifndef tr_obj_h
#define tr_obj_h

typedef enum {
  OBJ_NULL,
  OBJ_FUNC,
  OBJ_CLOSURE
} tr_obj_type;

struct tr_object {
  tr_obj_type type;
  void (*destruct)(struct tr_object *object);
};

void tr_object_init(struct tr_object *obj, int type);
void tr_object_destroy(struct tr_object *obj);

#endif
