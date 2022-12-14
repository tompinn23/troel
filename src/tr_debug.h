#ifndef tr_debug_h
#define tr_debug_h

#include "tr_value.h"

struct tr_chunk;

void tr_chunk_disassemble(struct tr_chunk *chunk, const char *name);
int tr_opcode_dissasemble(struct tr_chunk *chunk, int offset);

void tr_debug_print_val(struct tr_value *val, char *buf, int len);
const char *tr_debug_value_type(struct tr_value *val);

#endif