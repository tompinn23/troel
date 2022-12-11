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

static void emit_opcode(struct tr_parser* p, uint8_t opcode) {
  tr_chunk_add(&p->compiler->function->chunk, opcode);
}

static int emit_constant(struct tr_parser* p, struct tr_value val) {
  int id = tr_constants_add(&p->compiler->function->chunk.constants, val);
  if (id > UINT8_MAX) {
    error(p, "Too many constants in one chunk (function, etc.)");
    id = 0;
  }
  emit_opcode(p, OP_CONSTANT);
  emit_opcode(p, id);
  return id;
}

static void advance(struct tr_parser* p) {
  if (p->preprevious.start != NULL) {
    mem_free(p->preprevious.start);
  }
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
    emit_opcode(p, OP_NOT);
    break;
  case TOKEN_MINUS:
    emit_opcode(p, OP_NEGATE);
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
    emit_opcode(p, OP_EQUAL);
    break;
  case TOKEN_NE:
    emit_opcode(p, OP_NEQUAL);
    break;
  case TOKEN_GT:
    emit_opcode(p, OP_GT);
    break;
  case TOKEN_GTEQ:
    emit_opcode(p, OP_GTEQ);
    break;
  case TOKEN_LT:
    emit_opcode(p, OP_LT);
    break;
  case TOKEN_LTEQ:
    emit_opcode(p, OP_LTEQ);
    break;
  case TOKEN_PLUS:
    emit_opcode(p, floating ? OP_FADD : OP_IADD);
    break;
  case TOKEN_MINUS:
    emit_opcode(p, floating ? OP_FSUB : OP_ISUB);
    break;
  case TOKEN_STAR:
    emit_opcode(p, floating ? OP_FMUL : OP_IMUL);
    break;
  case TOKEN_SLASH:
    emit_opcode(p, floating ? OP_FDIV : OP_IDIV);
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
    emit_opcode(p, OP_FALSE);
    break;
  case TOKEN_TRUE:
    emit_opcode(p, OP_TRUE);
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
  emit_opcode(p, OP_POP);
}

static uint8_t ident_constant(struct tr_parser* p, struct tr_token* name) {
  struct tr_string s;
  tr_string_ncpy(&s, name->start, name->length);
  int id = tr_constants_add(&p->compiler->function->chunk.constants,
                            (struct tr_value){.type = VAL_STR, .s = s});
  if (id > UINT8_MAX) {
    error(p, "Too many constants in one chunk (function, etc.)");
    id = 0;
  }
  return id;
}

static void add_local(struct tr_parser* p, struct tr_token name) {
  if (p->compiler->local_count == UINT8_MAX + 1) {
    error(p, "Too many local variables in function.");
    return;
  }
  struct tr_local* local = &p->compiler->locals[p->compiler->local_count++];
  local->name            = tr_token_cpy(name);
  local->depth           = -1;
}

static bool identifier_equals(struct tr_token* a, struct tr_token* b) {
  if (a->length != b->length)
    return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static void mark_initialized(struct tr_parser* p) {
  if (p->compiler->scope_depth == 0)
    return;
  p->compiler->locals[p->compiler->local_count - 1].depth = p->compiler->scope_depth;
}

static void declare_variable(struct tr_parser* p) {
  if (p->compiler->scope_depth == 0) {
    return;
  }
  struct tr_token* name = &p->previous;
  for (int i = p->compiler->local_count - 1; i >= 0; i--) {
    struct tr_local* local = &p->compiler->locals[i];
    if (local->depth != -1 && local->depth < p->compiler->scope_depth) {
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
  if (p->compiler->scope_depth > 0)
    return 0;
  return ident_constant(p, &p->previous);
}

static void define_global(struct tr_parser* p, uint8_t global) {
  if (p->compiler->scope_depth > 0) {
    mark_initialized(p);
    return;
  }
  emit_opcode(p, OP_DEFINE_GLOBAL);
  emit_opcode(p, global);
}

static void var_declaration(struct tr_parser* p) {
  uint8_t global = parse_variable(p, "Expected a variable name.");
  if (match(p, TOKEN_ASSIGN)) {
    expression(p);
  } else {
    emit_opcode(p, OP_NIL);
  }
  consume(p, TOKEN_SEMICOLON, "Expected ';' after variable declaration");
  define_global(p, global);
}

static int resolve_local_up(struct tr_parser* p, struct tr_compiler* c, struct tr_token* name) {
  for (int i = c->local_count - 1; i >= 0; i--) {
    struct tr_local* local = &c->locals[i];
    if (identifier_equals(name, &local->name)) {
      if (local->depth == -1) {
        error(p, "Can't read local variable in its own initializer");
      }
      return i;
    }
  }
  return -1;
}

static int resolve_local(struct tr_parser* p, struct tr_token* name) {
  return resolve_local_up(p, p->compiler, name);
}

static int add_upvalue(struct tr_compiler* c, uint8_t index, bool isLocal) {
  for (int i = 0; i < c->function->upvalue_count; i++) {
    struct tr_upvalue* v = &c->upvalues[i];
    if (v->index == index && v->is_local == isLocal)
      return i;
  }
  c->upvalues[c->function->upvalue_count++] = (struct tr_upvalue){isLocal, index};
  return c->function->upvalue_count;
}

static int resolve_upvalue(struct tr_parser* p, struct tr_token* name) {
  if (p->compiler->enclosing == NULL)
    return -1;
  int local = resolve_local_up(p, p->compiler->enclosing, name);
  if (local != -1) {
    return add_upvalue(p->compiler, (uint8_t)local, true);
  }
  return -1;
}

static void variable(struct tr_parser* p, bool canAssign) {
  uint8_t get_op, set_op;
  int arg = resolve_local(p, &p->previous);
  if (arg != -1) {
    get_op = OP_GET_LOCAL;
    set_op = OP_SET_LOCAL;
  } else if ((arg = resolve_upvalue(p, &p->previous)) != -1) {
    get_op = OP_GET_UPVAL;
    set_op = OP_SET_UPVAL;
  } else {
    arg    = ident_constant(p, &p->previous);
    get_op = OP_GET_GLOBAL;
    set_op = OP_SET_GLOBAL;
  }
  if (canAssign && match(p, TOKEN_ASSIGN)) {
    expression(p);
    emit_opcode(p, set_op);
  } else {
    emit_opcode(p, get_op);
  }
  emit_opcode(p, arg);
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
//     emit_opcode(p, OP_NIL);
//   }
//   consume(p, TOKEN_SEMICOLON, "Expected ';' after variable declaration");
//   emit_opcode(p, OP_DEFINE_GLOBAL);
//   emit_opcode(p, local);
// }

static void patch_jump(struct tr_parser* p, int jump) {
  int j = p->compiler->function->chunk.count - jump - 2;
  if (j > UINT16_MAX) {
    error(p, "Too much code to jump");
  }
  p->compiler->function->chunk.instructions[jump]     = (j >> 8) & 0xff;
  p->compiler->function->chunk.instructions[jump + 1] = j & 0xff;
}

static int emit_jump(struct tr_parser* p, int opcode) {
  emit_opcode(p, opcode);
  emit_opcode(p, 0xff);
  emit_opcode(p, 0xff);
  return p->compiler->function->chunk.count - 2;
}

static void if_statement(struct tr_parser* p) {
  consume(p, TOKEN_L_PAREN, "Expected '(' after if.");
  expression(p);
  consume(p, TOKEN_R_PAREN, "Expected ')' after condition");
  int jump = emit_jump(p, OP_JMP_FALSE);
  emit_opcode(p, OP_POP);
  statement(p);
  int else_jump = emit_jump(p, OP_JMP);
  patch_jump(p, jump);
  emit_opcode(p, OP_POP);
  if (match(p, TOKEN_ELSE))
    statement(p);
  patch_jump(p, else_jump);
}

static void emit_loop(struct tr_parser* p, int loop_start) {
  emit_opcode(p, OP_LOOP);
  int offset = p->compiler->function->chunk.count - loop_start + 2;
  if (offset > UINT16_MAX)
    error(p, "loop body too large.");
  emit_opcode(p, (offset >> 8) & 0xFF);
  emit_opcode(p, offset & 0xFF);
}

static void while_statement(struct tr_parser* p) {
  int loop_start = p->compiler->function->chunk.count;
  consume(p, TOKEN_L_PAREN, "Expecting '(' after while.");
  expression(p);
  consume(p, TOKEN_R_PAREN, "Expecting ')' after expression.");
  int exit_jump = emit_jump(p, OP_JMP_FALSE);
  emit_opcode(p, OP_POP);
  statement(p);
  emit_loop(p, loop_start);
  patch_jump(p, exit_jump);
  emit_opcode(p, OP_POP);
}

static void begin_scope(struct tr_parser* p) { p->compiler->function->locals.scopeDepth++; }

static void end_scope(struct tr_parser* p) {
  p->compiler->function->locals.scopeDepth--;
  while (p->compiler->function->locals.local_count > 0 &&
         p->compiler->function->locals.locals[p->function->locals.local_count - 1].depth >
             p->compiler->function->locals.scopeDepth) {
    emit_opcode(p, OP_POP);
    p->compiler->function->locals.local_count--;
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
  int loop_start = p->compiler->function->chunk.count;
  int exit_jump  = -1;
  if (!match(p, TOKEN_SEMICOLON)) {
    expression(p);
    consume(p, TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    exit_jump = emit_jump(p, OP_JMP_FALSE);
    emit_opcode(p, OP_POP);
  }
  if (!match(p, TOKEN_R_PAREN)) {
    int body_jump       = emit_jump(p, OP_JMP);
    int increment_start = p->compiler->function->chunk.count;
    expression(p);
    emit_opcode(p, OP_POP);
    consume(p, TOKEN_R_PAREN, "Expect ')' after for clauses.");

    emit_loop(p, loop_start);
    loop_start = increment_start;
    patch_jump(p, body_jump);
  }
  statement(p);
  emit_loop(p, loop_start);
  if (exit_jump != -1) {
    patch_jump(p, exit_jump);
    emit_opcode(p, OP_POP);
  }
  end_scope(p);
}

static void parser_init_func(struct tr_parser* p, struct tr_func* new, int fn_type) {
  new->enclosing        = p->compiler->function;
  p->compiler->function = NULL;
  p->type               = fn_type;
  p->compiler->function = new;
  if (fn_type != TYPE_SCRIPT) {
    p->compiler->function->name = tr_string_new_ncpy(p->previous.start, p->previous.length);
  }
  struct tr_local* local = &p->compiler->function->locals.locals[p->function->locals.local_count++];
  local->depth           = 0;
  // local->isCaptured      = 0;
}

static void parser_end_func(struct tr_parser* p) {
  emit_opcode(p, OP_NIL);
  emit_opcode(p, OP_RETURN);
#ifdef DEBUG_PRINT_CODE
  if (!p->error) {
    tr_chunk_disassemble(&p->compiler->function->chunk,
                         p->compiler->function->name != NULL ? p->function->name->str : "<script>");
  }
#endif
  p->compiler->function = p->function->enclosing;
}

static void block(struct tr_parser* p) {
  while (!check(p, TOKEN_R_BRACE) && !check(p, TOKEN_EOF)) {
    declaration(p);
  }
  consume(p, TOKEN_R_BRACE, "Expected } after block.");
}

static void function(struct tr_parser* p, int function_type) {
  struct tr_func* func = tr_func_new();
  parser_init_func(p, func, function_type);
  begin_scope(p);
  consume(p, TOKEN_L_PAREN, "Expected ( after function name");
  if (!check(p, TOKEN_R_PAREN)) {
    do {
      p->compiler->function->arity++;
      if (p->compiler->function->arity > 255) {
        error_current(p, "Can't have more than 255 parameters, you mad man.");
      }
      uint8_t constant = parse_variable(p, "Expect parameter name");
      define_global(p, constant);
    } while (match(p, TOKEN_COMMA));
  }
  consume(p, TOKEN_R_PAREN, "Expected ) after function name");
  consume(p, TOKEN_L_BRACE, "Expected  { before function body");
  block(p);
  parser_end_func(p);
  emit_opcode(p, OP_CLOSURE);
  emit_opcode(p, emit_constant(p, OBJ_VALUE(func)));
}

static void func_declaration(struct tr_parser* p) {
  uint8_t global = parse_variable(p, "Expected function name");
  mark_initialized(p);
  function(p, TYPE_FUNC);
  define_global(p, global);
}

static uint8_t argument_list(struct tr_parser* p) {
  uint8_t count = 0;
  if (!check(p, TOKEN_R_PAREN)) {
    do {
      expression(p);
      if (count == 255) {
        error(p, "Can't have more than 255 arguments");
      }
      count++;
    } while (match(p, TOKEN_COMMA));
  }
  consume(p, TOKEN_R_PAREN, "Expected ')' after arguments.");
  return count;
}

static void call(struct tr_parser* p, bool ca) {
  uint8_t arg_count = argument_list(p);
  emit_opcode(p, OP_CALL);
  emit_opcode(p, arg_count);
}

static void emit_return(struct tr_parser* p) {
  emit_opcode(p, OP_NIL);
  emit_opcode(p, OP_RETURN);
}

static void return_statement(struct tr_parser* p) {
  if (p->compiler->function->type == TYPE_SCRIPT) {
    error(p, "Can't return from the script lol");
  }
  if (match(p, TOKEN_SEMICOLON)) {
    emit_return(p);
  } else {
    expression(p);
    consume(p, TOKEN_SEMICOLON, "Expected ; after return val");
    emit_opcode(p, OP_RETURN);
  }
}
static void declaration(struct tr_parser* p) {
  if (match(p, TOKEN_FUNC)) {
    func_declaration(p);
  } else if (match(p, TOKEN_VAR)) {
    var_declaration(p);
  } else if (match(p, TOKEN_FOR)) {
    for_statement(p);
  } else if (match(p, TOKEN_IF)) {
    if_statement(p);
  } else if (match(p, TOKEN_RETURN)) {
    return_statement(p);
  } else if (match(p, TOKEN_WHILE)) {
    while_statement(p);
  } else {
    statement(p);
  }
  if (p->panicking)
    synchronize(p);
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
  emit_opcode(p, OP_POP);
  precedence(p, PREC_AND);
  patch_jump(p, end_jump);
}

static void or_(struct tr_parser* p, bool ca) {
  int else_jump = emit_jump(p, OP_JMP_FALSE);
  int end_jump  = emit_jump(p, OP_JMP);
  patch_jump(p, else_jump);
  emit_opcode(p, OP_POP);

  precedence(p, PREC_OR);
  patch_jump(p, end_jump);
}

static struct tr_parse_rule rules[] = {
    [TOKEN_L_PAREN]   = {grouping, call,   PREC_CALL  },
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
  p->error = p->panicking     = false;
  p->compiler->function       = tr_func_new();
  p->compiler->function->type = TYPE_SCRIPT;
  memset(&p->preprevious, 0, sizeof(p->preprevious));
  memset(&p->previous, 0, sizeof(p->current));
  memset(&p->current, 0, sizeof(p->current));

  struct tr_local* local = &p->compiler->locals[p->compiler->local_count++];
  local->depth           = 0;
  local->name.start      = "";
  local->name.length     = 0;
}

bool tr_parser_compile(struct tr_parser* parser) {
  advance(parser);
  while (!match(parser, TOKEN_EOF)) {
    declaration(parser);
  }
  emit_opcode(parser, OP_NIL);
  emit_opcode(parser, OP_RETURN);
#ifdef DEBUG_PRINT_CODE
  if (!parser->error) {
    tr_chunk_disassemble(&parser->compiler->function->chunk,
                         parser->compiler->function->name != NULL
                             ? parser->compiler->function->name->str
                             : "<script>");
  }
#endif
  consume(parser, TOKEN_EOF, "Epected EOF after expression");
  return !parser->error;
}
