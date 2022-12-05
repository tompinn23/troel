#ifndef tr_parser_h
#define tr_parser_h

#include "tr_lexer.h"
#include "tr_vm.h"
#include <stdbool.h>

struct tr_parser {
  struct tr_lexer* lexer;
  struct tr_chunk* chunk;
  struct tr_token preprevious;
  struct tr_token previous;
  struct tr_token current;

  bool error;
  bool panicking;
};

void tr_parser_init(struct tr_parser* p, struct tr_lexer* l);

bool tr_parser_compile(struct tr_parser* parser, struct tr_chunk* chunk);

#endif // tr_parser_h