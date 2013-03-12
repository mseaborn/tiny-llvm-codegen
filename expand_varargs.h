
#ifndef EXPAND_VARARGS_
#define EXPAND_VARARGS_ 1

#include <llvm/Pass.h>

llvm::ModulePass *createExpandVarArgsPass();

#endif
