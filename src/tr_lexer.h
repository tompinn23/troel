#ifndef tr_lexer_h
#define tr_lexer_h

#include <stdbool.h>
#include <stdlib.h>

typedef enum {
  TOKEN_L_PAREN,
  TOKEN_R_PAREN,
  TOKEN_L_BRACE,
  TOKEN_R_BRACE,
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_MINUS,
  TOKEN_PLUS,
  TOKEN_SEMICOLON,
  TOKEN_SLASH,
  TOKEN_STAR,
  TOKEN_ASSIGN,
  TOKEN_EXCL,
  TOKEN_AMPERSAND,
  TOKEN_PIPE,

  TOKEN_NE,
  TOKEN_EQ,
  TOKEN_GT,
  TOKEN_LT,
  TOKEN_GTEQ,
  TOKEN_LTEQ,
  TOKEN_AND,
  TOKEN_OR,

  TOKEN_IDENT,
  TOKEN_STRING,
  TOKEN_INT,
  TOKEN_NUMBER,

  TOKEN_CLASS,
  TOKEN_SUPER,
  TOKEN_THIS,

  TOKEN_FUNC,
  TOKEN_RETURN,
  TOKEN_BREAK,
  TOKEN_IF,
  TOKEN_ELSE,
  TOKEN_WHILE,
  TOKEN_FOR,

  TOKEN_NIL,
  TOKEN_VAR,
  TOKEN_TRUE,
  TOKEN_FALSE,

  TOKEN_ERR,
  TOKEN_EOF
} token_type;

struct tr_token {
  token_type type;
  char *start;
  int length;
  int line;
};

struct tr_lexer {
  char source[512];
  int size;
  const char *start;
  const char *current;
  int line;
  bool eof;

  int (*getc)(struct tr_lexer *l);
  int (*peekc)(struct tr_lexer *l, int far);
  // int (*eof)(struct tr_lexer *l);
  void *user;
};

void tr_lexer(struct tr_lexer *lex);

int tr_lexer_str_init(struct tr_lexer *lex, const char *string);
int tr_lexer_file_init(struct tr_lexer *l, const char *file);
struct tr_token tr_lexer_next_token(struct tr_lexer *l);

struct tr_token tr_token_cpy(struct tr_token o);

#endif
