//===- codegen.h - Fast and stupid code generator--------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CODEGEN_H_
#define CODEGEN_H_

#include <map>

#include <llvm/Module.h>

class CodeGenOptions {
public:
  CodeGenOptions(): dump_code(false), trace_logging(false) {}

  // Output disassembly of each function that is generated, using objdump.
  bool dump_code;
  // Generate code with log messages to trace execution.
  bool trace_logging;
};

void translate(llvm::Module *module, std::map<std::string,uintptr_t> *globals,
               CodeGenOptions *options);

#endif
