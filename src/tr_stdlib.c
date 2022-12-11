#include "tr_stdlib.h"
#include "tr_debug.h"
#include "tr_vm.h"
#include <stdio.h>
#include <time.h>

struct tr_value tr_print(struct tr_vm* vm, int args, struct tr_value* vals) {
  if (args != 1) {
    return INT_VALUE(-1);
  }
  struct tr_value s = vals[0];
  char buf[256];
  tr_debug_print_val(&s, buf, sizeof(buf));
  printf("TR OUTPUT: %s\n", buf);
  return INT_VALUE(0);
}

struct tr_value tr_clock(struct tr_vm* vm, int args, struct tr_value* vals) {
  return DOUBLE_VALUE((double)clock() / CLOCKS_PER_SEC);
}

void tr_stdlib_open(struct tr_vm* vm) {
  tr_vm_add_cfunc(vm, "print", tr_print);
  tr_vm_add_cfunc(vm, "clock", tr_clock);
}
