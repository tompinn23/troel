#ifndef tr_vm_h
#define tr_vm_h

#include <stdint.h>

#ifndef NDEBUG
#define TR_DEBUG_TRACE
#endif

#define STACK_MAX 4096

enum { VAL_STR, VAL_LNG, VAL_DBL, VAL_PTR, VAL_BOOL };

struct tr_value {
  int type;
  union {
    char* s;
    long l;
    double d;
    void* p;
  };
};

struct tr_constants {
  int count;
  int capacity;
  struct tr_value* values;
};

struct tr_chunk {
  struct tr_constants constants;
  int count;
  int capacity;
  uint8_t* instructions;
};

typedef enum { TR_VM_E_OK, TR_VM_E_RUNTIME, TR_VM_E_COMPILE } tr_vm_result;

struct tr_vm {
  struct tr_chunk* main;
  uint8_t* ip;
  struct tr_value stack[STACK_MAX];
  struct tr_value* stackTop;
};

void tr_chunk_init(struct tr_chunk* chunk);
void tr_chunk_free(struct tr_chunk* chunk);
void tr_chunk_add(struct tr_chunk* chunk, uint8_t instruction);

void tr_constants_init(struct tr_constants* constants);
void tr_constants_free(struct tr_constants* constants);
int tr_constants_add(struct tr_constants* constants, struct tr_value val);
struct tr_value* tr_constants_get(struct tr_constants* constants, int index);

void tr_vm_init(struct tr_vm* vm);
void tr_vm_free(struct tr_vm* vm);

void tr_vm_push(struct tr_vm* vm, struct tr_value val);
struct tr_value tr_vm_pop(struct tr_vm* vm);

int tr_vm_do_chunk(struct tr_vm* vm, struct tr_chunk* chunk);

#endif // tr_vm_h