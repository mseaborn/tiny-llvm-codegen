//===- expand_constantexpr.h - A pass for simplifying LLVM IR--------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef EXPAND_CONSTANTEXPR_
#define EXPAND_CONSTANTEXPR_ 1

#include <llvm/Pass.h>

llvm::FunctionPass *createExpandConstantExprPass();

#endif
