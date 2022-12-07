#include "tr_lexer.h"
#include "memory.h"

#include <stdbool.h>
#include <string.h>

static bool is_eof(struct tr_lexer* l) { return *l->current == '\0'; }

static struct tr_token make_token(struct tr_lexer* l, token_type type) {
  struct tr_token tok;
  tok.type   = type;
  tok.start  = l->start;
  tok.length = (int)(l->current - l->start);
  tok.line   = l->line;
  return tok;
}

static struct tr_token error_token(struct tr_lexer* l, const char* message) {
  struct tr_token tok;
  tok.type   = TOKEN_ERR;
  tok.start  = message;
  tok.length = strlen(message);
  tok.line   = l->line;
  return tok;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int advance(struct tr_lexer* l) {
  l->current++;
  return *(l->current - 1);
}

static int peek(struct tr_lexer* l) { return *l->current; }

static int peek2(struct tr_lexer* l) {
  if (is_eof(l))
    return '\0';
  return *(l->current + 1);
}

static bool match(struct tr_lexer* l, int ch) {
  if (is_eof(l))
    return false;
  if (*l->current != ch)
    return false;
  l->current++;
  return true;
}

static void skip_ws(struct tr_lexer* l) {
  for (;;) {
    int ch = peek(l);
    switch (ch) {
    case ' ':
    case '\r':
    case '\t':
      advance(l);
      break;
    case '\n':
      advance(l);
      break;
    case '/':
      if (peek2(l) == '/') {
        while (peek(l) != '\n' && !is_eof(l))
          advance(l);
      } else {
        return;
      }
    default:
      return;
    }
  }
}

static struct tr_token make_string(struct tr_lexer* l) {
  while (peek(l) != '"' && !is_eof(l)) {
    if (peek(l) == '\n')
      l->line++;
    advance(l);
  }
  if (is_eof(l))
    return error_token(l, "Unterminated String!");
  advance(l);
  return make_token(l, TOKEN_STRING);
}

static struct tr_token make_number(struct tr_lexer* l) {
  token_type type = TOKEN_INT;
  while (is_digit(peek(l)))
    advance(l);
  if (peek(l) == '.' && is_digit(peek2(l))) {
    type = TOKEN_NUMBER;
    advance(l);
  }
  while (is_digit(peek(l)))
    advance(l);
  return make_token(l, TOKEN_INT);
}

static token_type check_keyword(struct tr_lexer* l, int start, int len, const char* rest,
                                token_type type) {
  if (l->current - l->start == start + len && memcmp(l->start + start, rest, len) == 0) {
    return type;
  }
  return TOKEN_IDENT;
}
static token_type ident_type(struct tr_lexer* l) {
  // clang-format off
  switch (l->start[0]) {
  case 'a': return check_keyword(l, 1, 4, "lass", TOKEN_CLASS);
  case 's': return check_keyword(l, 1, 4, "uper", TOKEN_SUPER);
  case 't':
    if(l->current - l->start > 1) {
      switch(l->start[1]) {
      case 'h': return check_keyword(l, 2, 2, "is", TOKEN_THIS);
      case 'r': return check_keyword(l, 2, 2, "ue", TOKEN_TRUE);
      }
    }
    break;

  case 'f':
    if(l->current - l->start > 1) {
      switch(l->start[1]) {
      case 'a': return check_keyword(l, 2, 3, "lse", TOKEN_FALSE);
      case 'o': return check_keyword(l, 2, 1, "r", TOKEN_FOR);
      case 'n': return TOKEN_FUNC;
      }
    }
    break;
  case 'r': return check_keyword(l, 1, 5, "eturn", TOKEN_RETURN);

  case 'b': return check_keyword(l, 1, 4, "reak", TOKEN_BREAK);
  case 'i': return check_keyword(l, 1, 1, "f", TOKEN_IF);
  case 'e': return check_keyword(l, 1, 3, "lse", TOKEN_ELSE);
  case 'w': return check_keyword(l, 1, 4, "hile", TOKEN_WHILE);

  case 'n': return check_keyword(l, 1, 2, "il", TOKEN_NIL);
  case 'v': return check_keyword(l, 1, 2, "ar", TOKEN_VAR);
  }
  // clang-format on
  return TOKEN_IDENT;
}

static struct tr_token make_identifier(struct tr_lexer* l) {
  while (is_alpha(peek(l)) || is_digit(peek(l)))
    advance(l);
  return make_token(l, ident_type(l));
}

void tr_lexer_init(struct tr_lexer* lex) {
  lex->start = lex->current = NULL;
  lex->line                 = 0;
}

struct str_data {
  const char* source;
  int current;
  int max;
};

void tr_lexer_str_init(struct tr_lexer* lex, const char* string) {
  tr_lexer_init(lex);
  lex->source  = string;
  lex->current = string;
}

struct tr_token tr_lexer_next_token(struct tr_lexer* l) {
  skip_ws(l);
  l->start = l->current;
  if (is_eof(l))
    return make_token(l, TOKEN_EOF);

  int c = advance(l);

  if (is_alpha(c))
    return make_identifier(l);

  if (is_digit(c))
    return make_number(l);

  // clang-format off
  switch (c) {
    case '(': return make_token(l, TOKEN_L_PAREN);
    case ')': return make_token(l, TOKEN_R_PAREN);
    case '{': return make_token(l, TOKEN_L_BRACE);
    case '}': return make_token(l, TOKEN_R_BRACE);
    case ',': return make_token(l, TOKEN_COMMA);
    case '.': return make_token(l, TOKEN_DOT);
    case '-': return make_token(l, TOKEN_MINUS);
    case ';': return make_token(l, TOKEN_SEMICOLON);
    case '+': return make_token(l, TOKEN_PLUS);
    case '/': return make_token(l, TOKEN_SLASH);
    case '*': return make_token(l, TOKEN_STAR);
    case '&': return make_token(l, match(l, '&') ? TOKEN_AND : TOKEN_AMPERSAND);
    case '|': return make_token(l, match(l, '|') ? TOKEN_OR : TOKEN_PIPE);
    case '=': return make_token(l, match(l, '=') ? TOKEN_EQ : TOKEN_ASSIGN);
    case '!': return make_token(l, match(l, '=') ? TOKEN_NE : TOKEN_EXCL);
    case '<': return make_token(l, match(l, '=') ? TOKEN_LTEQ : TOKEN_LT);
    case '>': return make_token(l, match(l, '=') ? TOKEN_GTEQ : TOKEN_GT);
    case '"': return make_string(l);
  }
  // clang-format on
  return error_token(l, "Unknown Input");
}
