#ifndef tr_parser_h
#define tr_parser_h

#define DEBUG_PRINT_CODE

#include "tr_debug.h"
#include "tr_lexer.h"
#include "tr_vm.h"
#include <stdbool.h>

#define UINT8_COUNT UINT8_MAX + 1

struct tr_local {
  struct tr_token name;
  int depth;
  bool is_captured;
};

struct tr_upvalue {
  uint8_t index;
  bool is_local;
};

struct tr_compiler {
  struct tr_compiler* enclosing;
  struct tr_func* function;
  int type;

  struct tr_local locals[UINT8_COUNT];
  int local_count;

  struct tr_upvalue upvalues[UINT8_COUNT];
  int scope_depth;
};

struct tr_parser {
  struct tr_lexer* lexer;
  struct tr_compiler* compiler;
  tr_func_type type;
  struct tr_token preprevious;
  struct tr_token previous;
  struct tr_token current;

  bool error;
  bool panicking;
};

void tr_parser_init(struct tr_parser* p, struct tr_lexer* l);

bool tr_parser_compile(struct tr_parser* parser);

#endif // tr_parser_h
