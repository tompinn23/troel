#include "tr_debug.h"

#include "tr_opcode.h"
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

void tr_debug_print_val(struct tr_value* val) {
  if (!val) {
    printf("Invalid\n");
    return;
  }
  switch (val->type) {
  case VAL_STR:
    printf("%s\n", val->s);
    break;
  case VAL_LNG:
    printf("%ld\n", val->l);
    break;
  case VAL_DBL:
    printf("%f\n", val->d);
    break;
  }
}

static int singleOperandOpcode(const char* name, struct tr_chunk* chunk, int offset) {
  printf("%s ", name);
  printf("%03d ", chunk->instructions[offset + 1]);
  struct tr_value* val = tr_constants_get(&chunk->constants, chunk->instructions[offset + 1]);
  tr_debug_print_val(val);
  return offset + 2;
}

int tr_opcode_dissasemble(struct tr_chunk* chunk, int offset) {
  printf("%04d ", offset);
  uint8_t opcode = chunk->instructions[offset];
  switch (opcode) {
  case OP_RETURN:
    return simpleOpcode("OP_RETURN", offset);
  case OP_NEGATE:
    return simpleOpcode("OP_NEGATE", offset);
  case OP_NO:
    return simpleOpcode("OP_NO", offset);
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
    printf("Unknown: %x\n", opcode);
    return offset + 1;
  }
}