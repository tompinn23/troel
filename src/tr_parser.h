#ifndef tr_parser_h
#define tr_parser_h

#include "tr_lexer.h"
#include "tr_vm.h"
#include <stdbool.h>

typedef enum { TYPE_FUNC, TYPE_SCRIPT } tr_func_type;

struct tr_parser {
  struct tr_lexer* lexer;
  struct tr_func* function;
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
