#include "tr_vm.h"

#include "memory.h"
#include "tr_opcode.h"

#include <stdarg.h>
#include <stdio.h>

void tr_chunk_init(struct tr_chunk* chunk) {
  chunk->count        = 0;
  chunk->capacity     = 0;
  chunk->instructions = NULL;
  tr_constants_init(&chunk->constants);
}

void tr_chunk_free(struct tr_chunk* chunk) {
  mem_free(chunk->instructions);
  tr_chunk_init(chunk);
}

void tr_chunk_add(struct tr_chunk* chunk, uint8_t instruction) {
  if (chunk->capacity < chunk->count + 1) {
    int new             = chunk->capacity == 0 ? 8 : chunk->capacity * 2;
    chunk->instructions = mem_realloc(chunk->instructions, sizeof(uint8_t) * chunk->capacity, sizeof(uint8_t) * new);
    chunk->capacity     = new;
  }
  chunk->instructions[chunk->count++] = instruction;
}

void tr_constants_init(struct tr_constants* constants) {
  constants->count    = 0;
  constants->capacity = 0;
  constants->values   = NULL;
}
void tr_constants_free(struct tr_constants* constants) {
  for (int i = 0; i < constants->count; i++) {
    if (constants->values[i].type == VAL_STR) {
      mem_free(constants->values[i].s);
    }
  }
  mem_free(constants->values);
}
int tr_constants_add(struct tr_constants* constants, struct tr_value val) {
  if (constants->capacity < constants->count + 1) {
    int new = constants->capacity == 0 ? 8 : constants->capacity * 2;
    constants->values =
        mem_realloc(constants->values, sizeof(struct tr_value) * constants->capacity, sizeof(struct tr_value) * new);
    constants->capacity = new;
  }
  int ret = constants->count;
  // Duplicate the string to make sure the constant owns its own memory.
  if (val.type == VAL_STR) {
    val.s = mem_strdup(val.s);
  }
  constants->values[constants->count++] = val;
  return ret;
}

struct tr_value* tr_constants_get(struct tr_constants* constants, int index) {
  if (index < 0 || index >= constants->count) {
    return NULL;
  }
  return &constants->values[index];
}

static void vm_reset_stack(struct tr_vm* vm) { vm->stackTop = vm->stack; }

void tr_vm_init(struct tr_vm* vm) {
  vm->main = NULL;
  vm->ip   = NULL;
  vm_reset_stack(vm);
}

void tr_vm_free(struct tr_vm* vm) { tr_vm_init(vm); }

void tr_vm_push(struct tr_vm* vm, struct tr_value val) {
  *vm->stackTop = val;
  vm->stackTop++;
}

struct tr_value tr_vm_pop(struct tr_vm* vm) {
  if (vm->stackTop == vm->stack) {
    return (struct tr_value){.type = VAL_PTR, .p = NULL};
  }
  vm->stackTop--;
  return *vm->stackTop;
}

static void tr_vm_runtime_err(struct tr_vm* vm, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);
  vm_reset_stack(vm);
}

static inline long tr_vm_ipop(struct tr_vm* vm) { return tr_vm_pop(vm).l; }
static inline double tr_vm_fpop(struct tr_vm* vm) { return tr_vm_pop(vm).d; }

#define IBINARY_OP(op)                                                                                                 \
  do {                                                                                                                 \
    long a = tr_vm_ipop(vm);                                                                                           \
    long b = tr_vm_ipop(vm);                                                                                           \
    tr_vm_push(vm, (struct tr_value){.type = VAL_LNG, .l = a op b});                                                   \
  } while (0)

#define FBINARY_OP(op)                                                                                                 \
  do {                                                                                                                 \
    double a = tr_vm_fpop(vm);                                                                                         \
    double b = tr_vm_fpop(vm);                                                                                         \
    tr_vm_push(vm, (struct tr_value){.type = VAL_DBL, .d = (double)((double)a op(double) b)});                         \
  } while (0)

int tr_vm_do_chunk(struct tr_vm* vm, struct tr_chunk* chunk) {
#define READ_BYTE() (*vm->ip++)
  vm->main = chunk;
  vm->ip   = chunk->instructions;
  for (;;) {
#ifdef TR_DEBUG_TRACE
    printf("STACK:\n");
    for (struct tr_value* slot = vm->stack; slot < vm->stackTop; slot++) {
      printf("\t");
      tr_debug_print_val(slot);
    }
    printf("\n");
    tr_opcode_dissasemble(chunk, (int)(vm->ip - chunk->instructions));
#endif
    uint8_t op;
    switch (op = READ_BYTE()) {
    case OP_RETURN:
      return TR_VM_E_OK;
    case OP_NEGATE:
      struct tr_value* val = vm->stackTop;
      if (val->type == VAL_LNG) {
        val->l = -val->l;
        break;
      } else if (val->type == VAL_DBL) {
        val->d = -val->d;
        break;
      } else if (val->type == VAL_PTR || val->type == VAL_STR) {
        tr_vm_runtime_err(vm, "Attempted to negate non number type");
        return TR_VM_E_RUNTIME;
      }
    case OP_NOT:
      tr_vm_runtime_err(vm, "Unimplemented OP_NOT");
      break;
    case OP_EQUAL:
      struct tr_value b = tr_vm_pop(vm);
      struct tr_value a = tr_vm_pop(vm);
      tr_vm_push() break;
    case OP_IADD:
      IBINARY_OP(+);
      break;
    case OP_ISUB:
      IBINARY_OP(-);
      break;
    case OP_IDIV:
      IBINARY_OP(/);
      break;
    case OP_IMUL:
      IBINARY_OP(*);
      break;
    case OP_FADD:
      FBINARY_OP(+);
      break;
    case OP_FSUB:
      FBINARY_OP(-);
      break;
    case OP_FDIV:
      FBINARY_OP(/);
      break;
    case OP_FMUL:
      FBINARY_OP(*);
      break;
    case OP_CONSTANT:
      uint8_t idx = READ_BYTE();
      tr_vm_push(vm, chunk->constants.values[idx]);
      break;
    }
  }
#undef READ_BYTE
}