
#ifndef CODEGEN_H_
#define CODEGEN_H_

#include <map>

#include <llvm/Module.h>

void translate(llvm::Module *module, std::map<std::string,uintptr_t> *globals);

#endif
