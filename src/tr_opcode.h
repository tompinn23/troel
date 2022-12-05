#ifndef tr_insn_h
#define tr_insn_h

enum {
  OP_NO = 0,
  OP_RETURN,
  OP_CONSTANT,
  OP_NEGATE,
  OP_NOT,
  OP_TRUE,
  OP_FALSE,
  OP_EQUAL,
  OP_NEQUAL,
  OP_IADD,
  OP_ISUB,
  OP_IDIV,
  OP_IMUL,
  OP_FADD,
  OP_FSUB,
  OP_FDIV,
  OP_FMUL
};

#endif // tr_insn_h