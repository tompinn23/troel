#include "tr_stdlib.h"
#include "tr_vm.h"
#include <stdio.h>
struct tr_value tr_print(struct tr_vm* vm, int args, struct tr_value* vals) {
  if (args != 1) {
    return INT_VALUE(-1);
  }
  struct tr_value s = vals[0];
  if (s.type != VAL_STR)
    return INT_VALUE(-1);
  printf("[TR OUTPUT] %s\n", s.s.str);
  return INT_VALUE(0);
}

void tr_stdlib_open(struct tr_vm* vm) { tr_vm_add_cfunc(vm, "print", tr_print); }
