//===- expand_varargs.h - A pass for simplifying LLVM IR-------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef EXPAND_VARARGS_
#define EXPAND_VARARGS_ 1

#include <llvm/Pass.h>

llvm::ModulePass *createExpandVarArgsPass();

#endif
