#include "tr_lexer.h"
#include "memory.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static struct tr_token make_token(struct tr_lexer* l, token_type type) {
  struct tr_token tok;
  tok.type           = type;
  tok.length         = (int)(l->current - l->source);
  tok.start          = mem_strndup(l->source, tok.length);
  tok.line           = l->line;
  l->source[l->size] = '\0';
  l->size            = 0;
  l->current         = l->source;
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
  l->source[l->size++] = l->getc(l);
  l->current++;
  return *(l->current - 1);
}

static int peek(struct tr_lexer* l) { return l->peekc(l, 0); }
static int peek2(struct tr_lexer* l) { return l->peekc(l, 1); }

static bool match(struct tr_lexer* l, int ch) {
  if (l->eof)
    return false;
  if (*l->current != ch)
    return false;
  l->getc(l);
  return true;
}

static void skip_ws(struct tr_lexer* l) {
  for (;;) {
    int ch = peek(l);
    switch (ch) {
    case ' ':
    case '\r':
    case '\t':
      l->getc(l);
      break;
    case '\n':
      l->line++;
      l->getc(l);
      break;
    case '/':
      if (peek2(l) == '/') {
        while (peek(l) != '\n' && !l->eof)
          l->getc(l);
      } else {
        return;
      }
    default:
      return;
    }
  }
}

static struct tr_token make_string(struct tr_lexer* l) {
  while (peek(l) != '"' && !l->eof) {
    if (peek(l) == '\n')
      l->line++;
    advance(l);
  }
  if (l->eof)
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
  if (l->current - l->source == start + len && memcmp(l->source + start, rest, len) == 0) {
    return type;
  }
  return TOKEN_IDENT;
}
static token_type ident_type(struct tr_lexer* l) {
  // clang-format off
  switch (l->source[0]) {
  case 'a': return check_keyword(l, 1, 4, "lass", TOKEN_CLASS);
  case 's': return check_keyword(l, 1, 4, "uper", TOKEN_SUPER);
  case 't':
    if(l->current - l->source > 1) {
      switch(l->source[1]) {
      case 'h': return check_keyword(l, 2, 2, "is", TOKEN_THIS);
      case 'r': return check_keyword(l, 2, 2, "ue", TOKEN_TRUE);
      }
    }
    break;

  case 'f':
    if(l->current - l->source > 1) {
      switch(l->source[1]) {
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
  memset(lex->source, 0, sizeof(lex->source));
  lex->current = NULL;
  lex->line    = 0;
  lex->current = lex->source;
  lex->size    = 0;
  lex->eof     = false;
}

int s_getc(struct tr_lexer* l) {
  char* s = l->user;
  if (*s == '\0') {
    l->eof = true;
    return *s;
  }
  s++;
  l->user = s;
  return *(s - 1);
}

int s_peekc(struct tr_lexer* l, int far) {
  char* s = l->user;
  for (int i = 0; i <= far; i++) {
    if (*(s + i) == '\0')
      return '\0';
  }
  return *(s + far);
}

struct f_data {
  FILE* fp;
  bool eof;
};

int f_getc(struct tr_lexer* l) {
  FILE* fp = (FILE*)l->user;
  char ch;
  if (fread(&ch, sizeof(char), 1, fp) != 1) {
    l->eof = true;
    return '\0';
  }
  return ch;
}

int f_peekc(struct tr_lexer* l, int far) {
  FILE* fp   = l->user;
  size_t off = ftell(fp);
  fseek(fp, far, SEEK_CUR);
  char ch;
  if (fread(&ch, sizeof(char), 1, fp) != 1) {
    fseek(fp, off, SEEK_SET);
    return '\0';
  }
  fseek(fp, off, SEEK_SET);
  return ch;
}

int tr_lexer_str_init(struct tr_lexer* lex, const char* string) {
  tr_lexer_init(lex);
  lex->user  = (void*)string;
  lex->getc  = s_getc;
  lex->peekc = s_peekc;
  return 0;
}

int tr_lexer_file_init(struct tr_lexer* lex, const char* file) {
  tr_lexer_init(lex);
  FILE* fp = fopen(file, "r");
  if (!fp) {
    return -1;
  }
  lex->user  = fp;
  lex->getc  = f_getc;
  lex->peekc = f_peekc;
  return 0;
}

struct tr_token tr_lexer_next_token(struct tr_lexer* l) {
  skip_ws(l);
  if (l->eof)
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

struct tr_token tr_token_cpy(struct tr_token o) {
  struct tr_token ret;
  ret.length = o.length;
  ret.line   = o.line;
  ret.type   = o.type;
  ret.start  = mem_strndup(o.start, o.length);
  return ret;
}
