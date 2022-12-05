#include "tr_parser.h"

#include "tr_opcode.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  PREC_NONE,
  PREC_ASSIGN,
  PREC_OR,
  PREC_AND,
  PREC_EQ,
  PREC_COMP,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY
};

typedef void (*parse_fn)(struct tr_parser* p);
struct tr_parse_rule {
  parse_fn prefix;
  parse_fn infix;
  int precedence;
};

static struct tr_parse_rule* tr_parser_get_rule(token_type type);

static void error_at(struct tr_parser* p, struct tr_token* t, const char* message) {
  if (p->panicking)
    return;
  p->panicking = true;
  fprintf(stderr, "[%d] err", t->line);
  if (t->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (t->type == TOKEN_ERR) {

  } else {
    fprintf(stderr, " at '%.*s'", t->length, t->start);
  }
  fprintf(stderr, ": %s\n", message);
  p->error = true;
}
static void error_current(struct tr_parser* p, const char* message) { error_at(p, &p->current, message); }
static void error(struct tr_parser* p, const char* message) { error_at(p, &p->previous, message); }

static void emit_constant(struct tr_parser* p, struct tr_value val) {
  int id = tr_constants_add(&p->chunk->constants, val);
  if (id > UINT8_MAX) {
    error(p, "Too many constants in one chunk (function, etc.)");
    id = 0;
  }
  tr_chunk_add(p->chunk, OP_CONSTANT);
  tr_chunk_add(p->chunk, id);
}

static void advance(struct tr_parser* p) {
  p->preprevious = p->previous;
  p->previous    = p->current;
  for (;;) {
    p->current = tr_lexer_next_token(p->lexer);
    if (p->current.type != TOKEN_ERR)
      break;
  }
}

static void consume(struct tr_parser* p, token_type type, const char* message) {
  if (p->current.type == type) {
    advance(p);
    return;
  }
  error_current(p, message);
}

void number(struct tr_parser* p) {
  if (p->previous.type == TOKEN_NUMBER) { // Decimal means floating points!!
    double val = strtod(p->previous.start, NULL);
    emit_constant(p, (struct tr_value){.type = VAL_DBL, .d = val});
  } else if (p->previous.type == TOKEN_INT) {
    long val = strtol(p->previous.start, NULL, 0);
    emit_constant(p, (struct tr_value){.type = VAL_LNG, .l = val});
  }
}

static void precedence(struct tr_parser* p, int prec) {
  advance(p);
  parse_fn prefixR = tr_parser_get_rule(p->previous.type)->prefix;
  if (prefixR == NULL) {
    error(p, "Expected expression.");
    return;
  }
  prefixR(p);
  while (prec < tr_parser_get_rule(p->current.type)->precedence) {
    advance(p);
    parse_fn infixR = tr_parser_get_rule(p->previous.type)->infix;
    infixR(p);
  }
}

static void expression(struct tr_parser* p) { precedence(p, PREC_ASSIGN); }

static void unary(struct tr_parser* p) {
  token_type type = p->previous.type;
  precedence(p, PREC_UNARY);
  switch (type) {
  case TOKEN_EXCL:
    tr_chunk_add(p->chunk, OP_NOT);
    break;
  case TOKEN_MINUS:
    tr_chunk_add(p->chunk, OP_NEGATE);
    break;
  default:
    return;
  }
}

// TODO: Determine floating point on right hand side ?
static void binary(struct tr_parser* p) {
  token_type left_hand       = p->preprevious.type;
  token_type type            = p->previous.type;
  struct tr_parse_rule* rule = tr_parser_get_rule(type);
  precedence(p, rule->precedence + 1);
  bool floating = p->previous.type == TOKEN_NUMBER || left_hand == TOKEN_NUMBER;
  switch (type) {
  case TOKEN_PLUS:
    tr_chunk_add(p->chunk, floating ? OP_FADD : OP_IADD);
    break;
  case TOKEN_MINUS:
    tr_chunk_add(p->chunk, floating ? OP_FSUB : OP_ISUB);
    break;
  case TOKEN_STAR:
    tr_chunk_add(p->chunk, floating ? OP_FMUL : OP_FMUL);
    break;
  case TOKEN_SLASH:
    tr_chunk_add(p->chunk, floating ? OP_FDIV : OP_IDIV);
    break;
  default:
    return;
  }
}

static void grouping(struct tr_parser* p) {
  expression(p);
  consume(p, TOKEN_R_PAREN, "Expects ')' after expression.");
}

static void literal(struct tr_parser* p) {
  switch (p->previous.type) {
  case TOKEN_FALSE:
    tr_chunk_add(p->chunk, OP_FALSE);
    break;
  case TOKEN_TRUE:
    tr_chunk_add(p->chunk, OP_TRUE);
    break;
  default:
    return;
  }
}

static struct tr_parse_rule rules[] = {
    [TOKEN_L_PAREN]   = {grouping, NULL,   PREC_NONE  },
    [TOKEN_R_PAREN]   = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_L_BRACE]   = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_R_BRACE]   = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_COMMA]     = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_DOT]       = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_MINUS]     = {unary,    binary, PREC_TERM  },
    [TOKEN_PLUS]      = {NULL,     binary, PREC_TERM  },
    [TOKEN_SEMICOLON] = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_SLASH]     = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]      = {NULL,     binary, PREC_FACTOR},
    [TOKEN_EXCL]      = {unary,    NULL,   PREC_NONE  },
    [TOKEN_NE]        = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_ASSIGN]    = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_EQ]        = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_GT]        = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_GTEQ]      = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_LT]        = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_LTEQ]      = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_IDENT]     = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_STRING]    = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_NUMBER]    = {number,   NULL,   PREC_NONE  },
    [TOKEN_INT]       = {number,   NULL,   PREC_NONE  },
 //[TOKEN_AND] = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_CLASS] = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_ELSE]  = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_FALSE] = {literal,  NULL,   PREC_NONE  },
    [TOKEN_FOR]   = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_FUNC]  = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_IF]    = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_NIL]   = {NULL,     NULL,   PREC_NONE  },
 //[TOKEN_OR] = {NULL,     NULL,   PREC_NONE  },
  //[TOKEN_PRINT] = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_RETURN] = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_SUPER]  = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_THIS]   = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_TRUE]   = {literal,  NULL,   PREC_NONE  },
    [TOKEN_VAR]    = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_WHILE]  = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_ERR]    = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_EOF]    = {NULL,     NULL,   PREC_NONE  },
};

static struct tr_parse_rule* tr_parser_get_rule(token_type type) { return &rules[type]; }

void tr_parser_init(struct tr_parser* p, struct tr_lexer* l) {
  p->lexer = l;
  p->error = p->panicking = false;
}

bool tr_parser_compile(struct tr_parser* parser, struct tr_chunk* chunk) {
  parser->chunk = chunk;
  advance(parser);
  expression(parser);

  tr_chunk_add(chunk, OP_RETURN);
#ifdef DEBUG_PRINT_CODE
  if (!parser.error) {
    tr_chunk_disassemble(chunk, "code");
  }
#endif
  consume(parser, TOKEN_EOF, "Epected EOF after expression");
}
