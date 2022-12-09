#include "tr_debug.h"
#include "tr_opcode.h"

#include "tr_lexer.h"
#include "tr_parser.h"
#include "tr_stdlib.h"
#include "tr_vm.h"

#include <stdio.h>

int main(int argc, char** argv) {
  struct tr_lexer lex;
  struct tr_parser p;
  tr_lexer_str_init(&lex, "fn Hello() { var b = \"Hello World\"; print(b); } Hello();");
  tr_parser_init(&p, &lex);
  if (!tr_parser_compile(&p)) {
    printf("Parsing failed!\n");
    return -1;
  }
  struct tr_vm* vm = tr_vm_new();
  tr_stdlib_open(vm);
  int ret = tr_vm_do_chunk(vm, p.function);
  if (ret != TR_VM_E_OK) {
    printf("An error occurred\n");
  }
  // tr_vm_free(&vm);
  return 0;
}
