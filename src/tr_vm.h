#ifndef tr_vm_h
#define tr_vm_h

#include <stdint.h>

#include "tr_lexer.h"
#include "tr_obj.h"
#include "tr_table.h"
#include "tr_value.h"

#ifndef NDEBUG
#define TR_DEBUG_TRACE
#endif

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * (UINT8_MAX + 1))

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

struct tr_local {
	struct tr_token name;
	int depth;
};

struct tr_locals {
	struct tr_local locals[UINT8_MAX + 1];
	int localCount;
	int scopeDepth;
};

struct tr_func {
  struct tr_object obj;
  int arity;
  struct tr_locals locals;
  struct tr_chunk chunk;
  struct tr_string* name;
};

struct tr_call_frame {
  struct tr_func* func;
  uint8_t* ip;
  struct tr_value* slots;
};

typedef enum { TR_VM_E_OK, TR_VM_E_RUNTIME, TR_VM_E_COMPILE } tr_vm_result;

struct tr_vm {
  struct tr_table globals;
  struct tr_value stack[STACK_MAX];
  struct tr_value* stackTop;
  struct tr_call_frame frames[FRAMES_MAX];
  int frame_count;
};

void tr_chunk_init(struct tr_chunk* chunk);
void tr_chunk_free(struct tr_chunk* chunk);
void tr_chunk_add(struct tr_chunk* chunk, uint8_t instruction);

void tr_constants_init(struct tr_constants* constants);
void tr_constants_free(struct tr_constants* constants);
int tr_constants_add(struct tr_constants* constants, struct tr_value val);
struct tr_value* tr_constants_get(struct tr_constants* constants, int index);

struct tr_func* tr_func_new();
void tr_func_destroy(struct tr_object* obj);

struct tr_vm* tr_vm_new();
void tr_vm_init(struct tr_vm* vm);
void tr_vm_free(struct tr_vm* vm);

void tr_vm_push(struct tr_vm* vm, struct tr_value val);
struct tr_value tr_vm_pop(struct tr_vm* vm);

int tr_vm_do_chunk(struct tr_vm* vm, struct tr_func* func);

#endif // tr_vm_h
