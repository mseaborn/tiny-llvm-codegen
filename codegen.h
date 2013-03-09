
#ifndef CODEGEN_H_
#define CODEGEN_H_

#include <map>

#include <llvm/Module.h>

class CodeGenOptions {
public:
  CodeGenOptions(): trace_logging(false) {}

  // Generate code with log messages to trace execution.
  bool trace_logging;
};

void translate(llvm::Module *module, std::map<std::string,uintptr_t> *globals,
               CodeGenOptions *options);

#endif
