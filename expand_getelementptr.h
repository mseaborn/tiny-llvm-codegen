//===- expand_getelementptr.h - A pass for simplifying LLVM IR-------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef EXPAND_GETELEMENTPTR_
#define EXPAND_GETELEMENTPTR_ 1

#include <llvm/Pass.h>

llvm::BasicBlockPass *createExpandGetElementPtrPass();

#endif
