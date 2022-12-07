#include "tr_parser.h"

#include "memory.h"
#include "tr_lexer.h"
#include "tr_opcode.h"
#include "tr_value.h"
#include "tr_vm.h"
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

struct type_mapping {
  const char* name;
  int type;
} internal_types[] = {
    {"int",    VAL_LNG},
    {"double", VAL_DBL},
    {"string", VAL_STR},
    {NULL,     0      }
};

typedef void (*parse_fn)(struct tr_parser* p, bool canAssign);
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
static void error_current(struct tr_parser* p, const char* message) {
  error_at(p, &p->current, message);
}
static void error(struct tr_parser* p, const char* message) { error_at(p, &p->previous, message); }

static void emit_constant(struct tr_parser* p, struct tr_value val) {
  int id = tr_constants_add(&p->function->chunk.constants, val);
  if (id > UINT8_MAX) {
    error(p, "Too many constants in one chunk (function, etc.)");
    id = 0;
  }
  tr_chunk_add(&p->function->chunk, OP_CONSTANT);
  tr_chunk_add(&p->function->chunk, id);
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

static bool check(struct tr_parser* p, token_type type) { return p->current.type == type; }

static bool match(struct tr_parser* p, token_type type) {
  if (!check(p, type))
    return false;
  advance(p);
  return true;
}

static void statement(struct tr_parser* p);
static void declaration(struct tr_parser* p);

void number(struct tr_parser* p, bool canAssign) {
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
  bool canAssign = prec <= PREC_ASSIGN;
  prefixR(p, canAssign);
  while (prec < tr_parser_get_rule(p->current.type)->precedence) {
    advance(p);
    parse_fn infixR = tr_parser_get_rule(p->previous.type)->infix;
    infixR(p, canAssign);
  }
  if (canAssign && match(p, TOKEN_ASSIGN)) {
    error(p, "Invalid assignment target.");
  }
}

static void expression(struct tr_parser* p) { precedence(p, PREC_ASSIGN); }

static void unary(struct tr_parser* p, bool canAssign) {
  token_type type = p->previous.type;
  precedence(p, PREC_UNARY);
  switch (type) {
  case TOKEN_EXCL:
    tr_chunk_add(&p->function->chunk, OP_NOT);
    break;
  case TOKEN_MINUS:
    tr_chunk_add(&p->function->chunk, OP_NEGATE);
    break;
  default:
    return;
  }
}

// TODO: Determine floating point on right hand side ?
static void binary(struct tr_parser* p, bool canAssign) {
  token_type left_hand       = p->preprevious.type;
  token_type type            = p->previous.type;
  struct tr_parse_rule* rule = tr_parser_get_rule(type);
  precedence(p, rule->precedence + 1);
  bool floating = p->previous.type == TOKEN_NUMBER || left_hand == TOKEN_NUMBER;
  switch (type) {
  case TOKEN_EQ:
    tr_chunk_add(&p->function->chunk, OP_EQUAL);
    break;
  case TOKEN_NE:
    tr_chunk_add(&p->function->chunk, OP_NEQUAL);
    break;
  case TOKEN_GT:
    tr_chunk_add(&p->function->chunk, OP_GT);
    break;
  case TOKEN_GTEQ:
    tr_chunk_add(&p->function->chunk, OP_GTEQ);
    break;
  case TOKEN_LT:
    tr_chunk_add(&p->function->chunk, OP_LT);
    break;
  case TOKEN_LTEQ:
    tr_chunk_add(&p->function->chunk, OP_LTEQ);
    break;
  case TOKEN_PLUS:
    tr_chunk_add(&p->function->chunk, floating ? OP_FADD : OP_IADD);
    break;
  case TOKEN_MINUS:
    tr_chunk_add(&p->function->chunk, floating ? OP_FSUB : OP_ISUB);
    break;
  case TOKEN_STAR:
    tr_chunk_add(&p->function->chunk, floating ? OP_FMUL : OP_IMUL);
    break;
  case TOKEN_SLASH:
    tr_chunk_add(&p->function->chunk, floating ? OP_FDIV : OP_IDIV);
    break;
  default:
    return;
  }
}

static void grouping(struct tr_parser* p, bool canAssign) {
  expression(p);
  consume(p, TOKEN_R_PAREN, "Expects ')' after expression.");
}

static void string(struct tr_parser* p, bool canAssign) {
  struct tr_string s;
  tr_string_ncpy(&s, p->previous.start + 1, p->previous.length - 2);
  emit_constant(p, (struct tr_value){.type = VAL_STR, .s = s});
}

static void literal(struct tr_parser* p, bool canAssign) {
  switch (p->previous.type) {
  case TOKEN_FALSE:
    tr_chunk_add(&p->function->chunk, OP_FALSE);
    break;
  case TOKEN_TRUE:
    tr_chunk_add(&p->function->chunk, OP_TRUE);
    break;
  default:
    return;
  }
}

static void synchronize(struct tr_parser* p) {
  p->panicking = false;
  while (p->current.type != TOKEN_EOF) {
    if (p->previous.type == TOKEN_SEMICOLON)
      return;
    switch (p->current.type) {
    case TOKEN_CLASS:
    case TOKEN_FUNC:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_RETURN:
      return;
    default:;
    }
    advance(p);
  }
}

static void expression_statement(struct tr_parser* p) {
  expression(p);
  consume(p, TOKEN_SEMICOLON, "Expecting a ';' after expression.");
  tr_chunk_add(&p->function->chunk, OP_POP);
}

static uint8_t ident_constant(struct tr_parser* p, struct tr_token* name) {
  struct tr_string s;
  tr_string_ncpy(&s, name->start, name->length);
  int id =
      tr_constants_add(&p->function->chunk.constants, (struct tr_value){.type = VAL_STR, .s = s});
  if (id > UINT8_MAX) {
    error(p, "Too many constants in one chunk (function, etc.)");
    id = 0;
  }
  return id;
}

static void add_local(struct tr_parser* p, struct tr_token name) {
  if (p->localCount == UINT8_MAX + 1) {
    error(p, "Too many local variables in function.");
    return;
  }
  struct tr_local* local = &p->locals[p->localCount++];
  local->name            = name;
  local->depth           = -1;
}

static bool identifier_equals(struct tr_token* a, struct tr_token* b) {
  if (a->length != b->length)
    return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static void mark_initialized(struct tr_parser* p) {
  p->locals[p->localCount - 1].depth = p->scopeDepth;
}

static void declare_variable(struct tr_parser* p) {
  if (p->scopeDepth == 0) {
    return;
  }
  struct tr_token* name = &p->previous;
  for (int i = p->localCount - 1; i >= 0; i--) {
    struct tr_local* local = &p->locals[i];
    if (local->depth != -1 && local->depth < p->scopeDepth) {
      break;
    }
    if (identifier_equals(name, &local->name)) {
      error(p, "Redeclaring previously declared variable");
    }
  }
  add_local(p, *name);
}

static uint8_t parse_variable(struct tr_parser* p, const char* err) {
  consume(p, TOKEN_IDENT, err);
  declare_variable(p);
  if (p->scopeDepth > 0)
    return 0;
  return ident_constant(p, &p->previous);
}

static void var_declaration(struct tr_parser* p) {
  uint8_t global = parse_variable(p, "Expected a variable name.");
  if (match(p, TOKEN_ASSIGN)) {
    expression(p);
  } else {
    tr_chunk_add(&p->function->chunk, OP_NIL);
  }
  consume(p, TOKEN_SEMICOLON, "Expected ';' after variable declaration");
  if (p->scopeDepth > 0) {
    mark_initialized(p);
    return;
  }
  tr_chunk_add(&p->function->chunk, OP_DEFINE_GLOBAL);
  tr_chunk_add(&p->function->chunk, global);
}

static int resolve_local(struct tr_parser* p, struct tr_token* name) {
  for (int i = p->localCount - 1; i >= 0; i--) {
    struct tr_local* local = &p->locals[i];
    if (identifier_equals(name, &local->name)) {
      if (local->depth == -1) {
        error(p, "Can't read local variable in its own initializer");
      }
      return i;
    }
  }
  return -1;
}

static void variable(struct tr_parser* p, bool canAssign) {
  uint8_t get_op, set_op;
  int arg = resolve_local(p, &p->previous);
  if (arg != -1) {
    get_op = OP_GET_LOCAL;
    set_op = OP_SET_LOCAL;
  } else {
    arg    = ident_constant(p, &p->previous);
    get_op = OP_GET_GLOBAL;
    set_op = OP_SET_GLOBAL;
  }
  if (canAssign && match(p, TOKEN_ASSIGN)) {
    expression(p);
    tr_chunk_add(&p->function->chunk, set_op);
  } else {
    tr_chunk_add(&p->function->chunk, get_op);
  }
  tr_chunk_add(&p->function->chunk, arg);
}

// static void typed_declaration(struct tr_parser* p) {
//   char* type_name        = mem_strndup(p->previous.start, p->previous.length);
//   struct type_mapping* m = internal_types;
//   while (m->name != NULL) {
//     if (strcmp(m->name, type_name) == 0) {
//       break;
//     }
//     m++;
//   }
//   if (m->name == NULL) {
//     error(p, "Type not found.");
//   }
//   uint8_t local = parse_variable(p, "Expected a variable name");
//   if (match(p, TOKEN_ASSIGN)) {
//     expression(p);
//   } else {
//     tr_chunk_add(&p->function->chunk, OP_NIL);
//   }
//   consume(p, TOKEN_SEMICOLON, "Expected ';' after variable declaration");
//   tr_chunk_add(&p->function->chunk, OP_DEFINE_GLOBAL);
//   tr_chunk_add(&p->function->chunk, local);
// }

static void patch_jump(struct tr_parser* p, int jump) {
  int j = p->function->chunk.count - jump - 2;
  if (j > UINT16_MAX) {
    error(p, "Too much code to jump");
  }
  p->function->chunk.instructions[jump]     = (j >> 8) & 0xff;
  p->function->chunk.instructions[jump + 1] = j & 0xff;
}

static int emit_jump(struct tr_parser* p, int opcode) {
  tr_chunk_add(&p->function->chunk, opcode);
  tr_chunk_add(&p->function->chunk, 0xff);
  tr_chunk_add(&p->function->chunk, 0xff);
  return p->function->chunk.count - 2;
}

static void if_statement(struct tr_parser* p) {
  consume(p, TOKEN_L_PAREN, "Expected '(' after if.");
  expression(p);
  consume(p, TOKEN_R_PAREN, "Expected ')' after condition");
  int jump = emit_jump(p, OP_JMP_FALSE);
  tr_chunk_add(&p->function->chunk, OP_POP);
  statement(p);
  int else_jump = emit_jump(p, OP_JMP);
  patch_jump(p, jump);
  tr_chunk_add(&p->function->chunk, OP_POP);
  if (match(p, TOKEN_ELSE))
    statement(p);
  patch_jump(p, else_jump);
}

static void emit_loop(struct tr_parser* p, int loop_start) {
  tr_chunk_add(&p->function->chunk, OP_LOOP);
  int offset = p->function->chunk.count - loop_start + 2;
  if (offset > UINT16_MAX)
    error(p, "loop body too large.");
  tr_chunk_add(&p->function->chunk, (offset >> 8) & 0xFF);
  tr_chunk_add(&p->function->chunk, offset & 0xFF);
}

static void while_statement(struct tr_parser* p) {
  int loop_start = p->function->chunk.count;
  consume(p, TOKEN_L_PAREN, "Expecting '(' after while.");
  expression(p);
  consume(p, TOKEN_R_PAREN, "Expecting ')' after expression.");
  int exit_jump = emit_jump(p, OP_JMP_FALSE);
  tr_chunk_add(&p->function->chunk, OP_POP);
  statement(p);
  emit_loop(p, loop_start);
  patch_jump(p, exit_jump);
  tr_chunk_add(&p->function->chunk, OP_POP);
}

static void begin_scope(struct tr_parser* p) { p->scopeDepth++; }

static void end_scope(struct tr_parser* p) {
  p->scopeDepth--;
  while (p->localCount > 0 && p->locals[p->localCount - 1].depth > p->scopeDepth) {
    tr_chunk_add(&p->function->chunk, OP_POP);
    p->localCount--;
  }
}

static void for_statement(struct tr_parser* p) {
  begin_scope(p);
  consume(p, TOKEN_L_PAREN, "Expect '(' after for.");
  if (match(p, TOKEN_SEMICOLON)) {

  } else if (match(p, TOKEN_VAR)) {
    var_declaration(p);
  } else {
    expression_statement(p);
  }
  int loop_start = p->function->chunk.count;
  int exit_jump  = -1;
  if (!match(p, TOKEN_SEMICOLON)) {
    expression(p);
    consume(p, TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    exit_jump = emit_jump(p, OP_JMP_FALSE);
    tr_chunk_add(&p->function->chunk, OP_POP);
  }
  if (!match(p, TOKEN_R_PAREN)) {
    int body_jump       = emit_jump(p, OP_JMP);
    int increment_start = p->function->chunk.count;
    expression(p);
    tr_chunk_add(&p->function->chunk, OP_POP);
    consume(p, TOKEN_R_PAREN, "Expect ')' after for clauses.");

    emit_loop(p, loop_start);
    loop_start = increment_start;
    patch_jump(p, body_jump);
  }
  statement(p);
  emit_loop(p, loop_start);
  if (exit_jump != -1) {
    patch_jump(p, exit_jump);
    tr_chunk_add(&p->function->chunk, OP_POP);
  }
  end_scope(p);
}

static void declaration(struct tr_parser* p) {
  if (match(p, TOKEN_VAR)) {
    var_declaration(p);
  } else if (match(p, TOKEN_FOR)) {
    for_statement(p);
  } else if (match(p, TOKEN_IF)) {
    if_statement(p);
  } else if (match(p, TOKEN_WHILE)) {
    while_statement(p);
  } else {
    statement(p);
  }
  if (p->panicking)
    synchronize(p);
}

static void block(struct tr_parser* p) {
  while (!check(p, TOKEN_R_BRACE) && !check(p, TOKEN_EOF)) {
    declaration(p);
  }
  consume(p, TOKEN_R_BRACE, "Expected } after block.");
}

static void statement(struct tr_parser* p) {
  if (match(p, TOKEN_L_BRACE)) {
    begin_scope(p);
    block(p);
    end_scope(p);
  } else {
    expression_statement(p);
  }
}

static void and_(struct tr_parser* p, bool c) {
  int end_jump = emit_jump(p, OP_JMP_FALSE);
  tr_chunk_add(&p->function->chunk, OP_POP);
  precedence(p, PREC_AND);
  patch_jump(p, end_jump);
}

static void or_(struct tr_parser* p, bool ca) {
  int else_jump = emit_jump(p, OP_JMP_FALSE);
  int end_jump  = emit_jump(p, OP_JMP);
  patch_jump(p, else_jump);
  tr_chunk_add(&p->function->chunk, OP_POP);

  precedence(p, PREC_OR);
  patch_jump(p, end_jump);
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
    [TOKEN_NE]        = {NULL,     binary, PREC_EQ    },
    [TOKEN_ASSIGN]    = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_EQ]        = {NULL,     binary, PREC_EQ    },
    [TOKEN_GT]        = {NULL,     binary, PREC_COMP  },
    [TOKEN_GTEQ]      = {NULL,     binary, PREC_COMP  },
    [TOKEN_LT]        = {NULL,     binary, PREC_COMP  },
    [TOKEN_LTEQ]      = {NULL,     binary, PREC_COMP  },
    [TOKEN_IDENT]     = {variable, NULL,   PREC_NONE  },
    [TOKEN_STRING]    = {string,   NULL,   PREC_NONE  },
    [TOKEN_NUMBER]    = {number,   NULL,   PREC_NONE  },
    [TOKEN_INT]       = {number,   NULL,   PREC_NONE  },
    [TOKEN_AND]       = {NULL,     and_,   PREC_NONE  },
    [TOKEN_CLASS]     = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_ELSE]      = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_FALSE]     = {literal,  NULL,   PREC_NONE  },
    [TOKEN_FOR]       = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_FUNC]      = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_IF]        = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_NIL]       = {NULL,     NULL,   PREC_NONE  },
    [TOKEN_OR]        = {NULL,     NULL,   PREC_NONE  },
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
  p->localCount           = 0;
  p->scopeDepth           = 0;
  p->function             = tr_func_new();
  p->type                 = TYPE_SCRIPT;
  struct tr_local* local  = &p->locals[p->localCount++];
  local->depth            = 0;
  local->name.start       = "";
  local->name.length      = 0;
}

bool tr_parser_compile(struct tr_parser* parser) {
  advance(parser);
  while (!match(parser, TOKEN_EOF)) {
    declaration(parser);
  }
  tr_chunk_add(&parser->function->chunk, OP_RETURN);
#ifdef DEBUG_PRINT_CODE
  if (!parser.error) {
    tr_chunk_disassemble(p->function->chunk,
                         p->function->name != NULL ? p->function->name->str : "<script>");
  }
#endif
  consume(parser, TOKEN_EOF, "Epected EOF after expression");
  return !parser->error;
}
