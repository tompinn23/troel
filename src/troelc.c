#include "tr_debug.h"
#include "tr_opcode.h"

#include "tr_lexer.h"
#include "tr_parser.h"
#include "tr_vm.h"

#include <stdio.h>

int main(int argc, char** argv) {
  struct tr_chunk chunk;
  tr_chunk_init(&chunk);
  struct tr_lexer lex;
  struct tr_parser p;
  tr_lexer_str_init(&lex, "(2 + 4) / 3");
  tr_parser_init(&p, &lex);
  tr_parser_compile(&p, &chunk);
  tr_chunk_disassemble(&chunk, "code");
  struct tr_vm vm;
  tr_vm_init(&vm);
  // int ret = tr_vm_do_chunk(&vm, &chunk);
  // if (ret != TR_VM_E_OK) {
  //   printf("An error occurred\n");
  // }
  tr_vm_free(&vm);
  tr_chunk_free(&chunk);
  return 0;
}