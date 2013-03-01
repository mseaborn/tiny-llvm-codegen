
#ifndef EXPAND_GETELEMENTPTR_
#define EXPAND_GETELEMENTPTR_ 1

#include <llvm/Pass.h>

llvm::BasicBlockPass *createExpandGetElementPtrPass();

#endif
