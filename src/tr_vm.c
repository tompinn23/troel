#include "tr_vm.h"

#include "memory.h"
#include "tr_debug.h"
#include "tr_opcode.h"
#include "tr_value.h"

#include <stdarg.h>
#include <stdio.h>

static int tr_vm_do_call_frame(struct tr_vm* vm, struct tr_call_frame* frame);

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
    int new = chunk->capacity == 0 ? 8 : chunk->capacity * 2;
    chunk->instructions =
        mem_realloc(chunk->instructions, sizeof(uint8_t) * chunk->capacity, sizeof(uint8_t) * new);
    chunk->capacity = new;
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
      mem_free(constants->values[i].s.str);
    }
  }
  mem_free(constants->values);
}
int tr_constants_add(struct tr_constants* constants, struct tr_value val) {
  if (constants->capacity < constants->count + 1) {
    int new = constants->capacity == 0 ? 8 : constants->capacity * 2;
    constants->values =
        mem_realloc(constants->values, sizeof(struct tr_value) * constants->capacity,
                    sizeof(struct tr_value) * new);
    constants->capacity = new;
  }
  int ret = constants->count;

  constants->values[constants->count++] = val;
  return ret;
}

struct tr_value* tr_constants_get(struct tr_constants* constants, int index) {
  if (index < 0 || index >= constants->count) {
    return NULL;
  }
  return &constants->values[index];
}

struct tr_func* tr_func_new() {
  struct tr_func* func = mem_alloc(sizeof(*func));
  tr_object_init(&func->obj);
  func->obj.destruct = tr_func_destroy;
  func->arity        = 0;
  func->name         = NULL;
  tr_chunk_init(&func->chunk);
  return func;
}

void tr_func_destroy(struct tr_object* obj) {
  struct tr_func* func = (struct tr_func*)obj;
  tr_chunk_free(&func->chunk);
  if (func->name != NULL) {
    tr_string_free(func->name);
  }
  mem_free(func);
}

static void vm_reset_stack(struct tr_vm* vm) { vm->stackTop = vm->stack; }

void tr_vm_init(struct tr_vm* vm) {
  vm_reset_stack(vm);
  tr_table_init(&vm->globals);
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

struct tr_value tr_vm_peek(struct tr_vm* vm, int idx) { return *(vm->stackTop - idx - 1); }

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

#define IBINARY_OP(op)                                                                             \
  do {                                                                                             \
    long a = tr_vm_ipop(vm);                                                                       \
    long b = tr_vm_ipop(vm);                                                                       \
    tr_vm_push(vm, (struct tr_value){.type = VAL_LNG, .l = a op b});                               \
  } while (0)

#define FBINARY_OP(op)                                                                             \
  do {                                                                                             \
    double a = tr_vm_fpop(vm);                                                                     \
    double b = tr_vm_fpop(vm);                                                                     \
    tr_vm_push(vm, (struct tr_value){.type = VAL_DBL, .d = (double)((double)a op(double) b)});     \
  } while (0)

#define VALUE_OP(op)                                                                               \
  do {                                                                                             \
    if (a.type != b.type)                                                                          \
      tr_vm_push(vm, (struct tr_value){.type = VAL_BOOL, .b = false});                             \
    break;                                                                                         \
    switch (a.type) {                                                                              \
    case VAL_BOOL:                                                                                 \
      tr_vm_push(vm, (struct tr_value){.type = VAL_BOOL, .b = a.l op b.l});                        \
      break;                                                                                       \
    case VAL_LNG:                                                                                  \
      tr_vm_push(vm, (struct tr_value){.type = VAL_BOOL, .b = a.l op b.l});                        \
      break;                                                                                       \
    case VAL_DBL:                                                                                  \
      tr_vm_push(vm, (struct tr_value){.type = VAL_BOOL, .b = a.d op b.d});                        \
      break;                                                                                       \
    case VAL_STR:                                                                                  \
      tr_vm_push(vm, (struct tr_value){.type = VAL_BOOL, .b = a.s.hash op b.s.hash});              \
      break;                                                                                       \
    default:                                                                                       \
      tr_vm_push(vm, (struct tr_value){.type = VAL_BOOL, .b = false});                             \
      break;                                                                                       \
    }                                                                                              \
  } while (0)

#define STRING_CONSTANT() (chunk->constants.values[READ_BYTE()].s)

int tr_vm_do_chunk(struct tr_vm* vm, struct tr_func* func) {
  tr_vm_push(vm, (struct tr_value){.type = VAL_OBJ, .obj = (struct tr_object*)func});
  struct tr_call_frame* frame = &vm->frames[vm->frame_count++];
  frame->func                 = func;
  frame->ip                   = func->chunk.instructions;
  frame->slots                = vm->stack;
  return tr_vm_do_call_frame(vm, frame);
}

static int tr_vm_do_call_frame(struct tr_vm* vm, struct tr_call_frame* frame) {
  struct tr_chunk* chunk = &frame->func->chunk;
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
  for (;;) {
#ifdef TR_DEBUG_TRACE
    printf("STACK:\n");
    for (struct tr_value* slot = vm->stack; slot < vm->stackTop; slot++) {
      printf("\t");
      tr_debug_print_val(slot);
    }
    printf("\n");
    tr_opcode_dissasemble(chunk, (int)(frame->ip - chunk->instructions));
#endif
    uint8_t op;
    switch (op = READ_BYTE()) {
    case OP_RETURN:
      return TR_VM_E_OK;
    case OP_JMP_FALSE: {
      uint16_t offt = READ_SHORT();
      if (tr_value_is_falsey(tr_vm_peek(vm, 0)))
        frame->ip += offt;
      break;
    }
    case OP_JMP: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      break;
    }
    case OP_LOOP: {
      uint16_t offset = READ_SHORT();
      frame->ip -= offset;
      break;
    }
    case OP_POP:
      tr_vm_pop(vm);
      break;
    case OP_NEGATE: {
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
    }
    case OP_DEFINE_GLOBAL: {
      uint8_t idx        = READ_BYTE();
      struct tr_string s = chunk->constants.values[idx].s;
      tr_table_insert(&vm->globals, &s, tr_vm_peek(vm, 0));
      tr_vm_pop(vm);
      break;
    }
    case OP_GET_GLOBAL: {
      uint8_t idx        = READ_BYTE();
      struct tr_string s = chunk->constants.values[idx].s;
      struct tr_value v;
      if (!tr_table_get(&vm->globals, &s, &v)) {
        tr_vm_runtime_err(vm, "Undefined global variable: %s", s.str);
        return TR_VM_E_RUNTIME;
      }
      tr_vm_push(vm, v);
      break;
    }
    case OP_SET_GLOBAL: {
      struct tr_string s = STRING_CONSTANT();
      if (tr_table_insert(&vm->globals, &s, tr_vm_peek(vm, 0))) {
        tr_table_delete(&vm->globals, &s);
        tr_vm_runtime_err(vm, "Attempted to assign to undeclared global");
        return TR_VM_E_RUNTIME;
      }
      break;
    }
    case OP_GET_LOCAL: {
      uint8_t slot = READ_BYTE();
      tr_vm_push(vm, frame->slots[slot]);
      break;
    }
    case OP_SET_LOCAL: {
      uint8_t slot       = READ_BYTE();
      frame->slots[slot] = tr_vm_peek(vm, 0);
      break;
    }
    case OP_NOT:
      switch (tr_vm_peek(vm, 0).type) {
      case VAL_BOOL:
        vm->stackTop->b = !vm->stackTop->b;
      case VAL_LNG: {
        if (vm->stackTop->l == 0) {
          vm->stackTop->l = 1;
        } else {
          vm->stackTop->l = 0;
        }
        break;
      }
      }
      break;
    case OP_EQUAL: {
      struct tr_value b = tr_vm_pop(vm);
      struct tr_value a = tr_vm_pop(vm);
      tr_value_eq(a, b);
      VALUE_OP(==);
      break;
    }
    case OP_NEQUAL: {
      struct tr_value b = tr_vm_pop(vm);
      struct tr_value a = tr_vm_pop(vm);
      VALUE_OP(!=);
      break;
    }
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
    case OP_CONSTANT: {
      uint8_t idx = READ_BYTE();
      tr_vm_push(vm, chunk->constants.values[idx]);
      break;
    }
    }
  }
#undef READ_BYTE
}
