
#ifndef EXPAND_CONSTANTEXPR_
#define EXPAND_CONSTANTEXPR_ 1

#include <llvm/Pass.h>

llvm::FunctionPass *createExpandConstantExprPass();

#endif
