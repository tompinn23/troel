#include "tr_debug.h"

#include "tr_opcode.h"
#include "tr_vm.h"
#include <stdio.h>

void tr_chunk_disassemble(struct tr_chunk* chunk, const char* name) {
  printf("Dissaembly: %s\n", name);
  for (int off = 0; off < chunk->count;) {
    off = tr_opcode_dissasemble(chunk, off);
  }
}

static int simpleOpcode(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

void tr_debug_print_val(struct tr_value* val, char* buf, int len) {
  if (!val) {
    snprintf(buf, len, "Invalid");
    return;
  }
  switch (val->type) {
  case VAL_STR:
    snprintf(buf, len, "%s", val->s.str);
    break;
  case VAL_LNG:
    snprintf(buf, len, "%ld", val->l);
    break;
  case VAL_DBL:
    snprintf(buf, len, "%f", val->d);
    break;
  case VAL_BOOL:
    snprintf(buf, len, "%s", val->b ? "true" : "false");
    break;
  case VAL_CFUNC:
    snprintf(buf, len, "<%p>", val->func);
    break;
  case VAL_OBJ:
    switch (val->obj->type) {
    case OBJ_NULL:
      snprintf(buf, len, "NULL");
      break;
    case OBJ_FUNC: {
      struct tr_func* fn = (struct tr_func*)val->obj;
      snprintf(buf, len, "<func: %s>", fn->name != NULL ? fn->name->str : "script");
      break;
    }
    }
    break;
  default:
    snprintf(buf, len, "Forgot to implement debugging for this type");
  }
}

const char* tr_debug_value_type(struct tr_value* val) {
  switch (val->type) {
  case VAL_BOOL:
    return "bool";
  case VAL_DBL:
    return "double";
  case VAL_LNG:
    return "long";
  case VAL_NIL:
    return "nil";
  case VAL_STR:
    return "string";
  case VAL_PTR:
    return "pointer";
  case VAL_CFUNC:
    return "cfunc<??>";
  case VAL_OBJ:
    switch (val->obj->type) {
    case OBJ_CLOSURE:
      return "object<closure>";
    case OBJ_FUNC:
      return "object<func>";
    case OBJ_NULL:
      return "object<null>";
    }
  default:
    return "unknown";
  }
}

static int singleOperandOpcode(const char* name, struct tr_chunk* chunk, int offset) {
  printf("%-16s ", name);
  printf("%03d ", chunk->instructions[offset + 1]);
  struct tr_value* val = tr_constants_get(&chunk->constants, chunk->instructions[offset + 1]);
  char buf[128];
  tr_debug_print_val(val, buf, sizeof(buf));
  printf("%s\n", buf);
  return offset + 2;
}

static int singleByteOpcode(const char* name, struct tr_chunk* chunk, int offset) {
  printf("%-16s ", name);
  printf("%03d\n", chunk->instructions[offset + 1]);
  return offset + 2;
}

static int jumpOpcode(const char* name, int sign, struct tr_chunk* chunk, int offset) {
  uint16_t jump = (uint16_t)(chunk->instructions[offset + 1] << 8);
  jump |= chunk->instructions[offset + 2];
  printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

int tr_opcode_dissasemble(struct tr_chunk* chunk, int offset) {
  printf("%04d ", offset);
  uint8_t opcode = chunk->instructions[offset];
  switch (opcode) {
  case OP_NIL:
    return simpleOpcode("OP_NIL", offset);
  case OP_RETURN:
    return simpleOpcode("OP_RETURN", offset);
  case OP_POP:
    return simpleOpcode("OP_POP", offset);
  case OP_NEGATE:
    return simpleOpcode("OP_NEGATE", offset);
  case OP_SET_LOCAL:
    return singleByteOpcode("OP_SET_LOCAL", chunk, offset);
  case OP_GET_LOCAL:
    return singleByteOpcode("OP_GET_LOCAL", chunk, offset);
  case OP_DEFINE_GLOBAL:
    return singleOperandOpcode("OP_DEFINE_GLOBAL", chunk, offset);
  case OP_SET_GLOBAL:
    return singleOperandOpcode("OP_SET_GLOBAL", chunk, offset);
  case OP_GET_GLOBAL:
    return singleOperandOpcode("OP_GET_GLOBAL", chunk, offset);
  case OP_JMP_FALSE:
    return jumpOpcode("OP_JMP_FALSE", 1, chunk, offset);
  case OP_JMP:
    return jumpOpcode("OP_JMP", 1, chunk, offset);
  case OP_LOOP:
    return jumpOpcode("OP_LOOP", -1, chunk, offset);
  case OP_CALL:
    return singleByteOpcode("OP_CALL",chunk, offset);
  case OP_CLOSURE: {
    char buf[256];
    offset++;
    uint8_t constant = chunk->instructions[offset++];
    tr_debug_print_val(&chunk->constants.values[constant], buf, sizeof(buf));
    printf("%-16s %03d %s\n", "OP_CLOSURE", constant, buf);
    return offset;
  }
  case OP_EQUAL:
    return simpleOpcode("OP_EQUAL", offset);
  case OP_NEQUAL:
    return simpleOpcode("OP_NEQUAL", offset);
  case OP_NO:
    return simpleOpcode("OP_NOP", offset);
  case OP_FALSE:
    return simpleOpcode("OP_FALSE", offset);
  case OP_TRUE:
    return simpleOpcode("OP_TRUE", offset);
  case OP_IADD:
    return simpleOpcode("OP_IADD", offset);
  case OP_ISUB:
    return simpleOpcode("OP_ISUB", offset);
  case OP_IDIV:
    return simpleOpcode("OP_IDIV", offset);
  case OP_IMUL:
    return simpleOpcode("OP_IMUL", offset);
  case OP_FADD:
    return simpleOpcode("OP_FADD", offset);
  case OP_FSUB:
    return simpleOpcode("OP_FSUB", offset);
  case OP_FDIV:
    return simpleOpcode("OP_FDIV", offset);
  case OP_FMUL:
    return simpleOpcode("OP_FMUL", offset);
  case OP_CONSTANT:
    return singleOperandOpcode("OP_CONSTANT", chunk, offset);
  default:
    printf("Unknown: %03d\n", opcode);
    return offset + 1;
  }
}
