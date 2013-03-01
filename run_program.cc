
#include <assert.h>
#include <stdio.h>

#include <llvm/LLVMContext.h>
#include <llvm/Support/IRReader.h>

#include "codegen.h"

int main(int argc, char **argv) {
  llvm::SMDiagnostic err;
  llvm::LLVMContext &context = llvm::getGlobalContext();

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <bitcode-file>\n", argv[0]);
    return 1;
  }
  const char *filename = argv[1];
  llvm::Module *module = llvm::ParseIRFile(filename, err, context);
  if (!module) {
    fprintf(stderr, "failed to read file: %s\n", filename);
    return 1;
  }

  std::map<std::string,uintptr_t> globals;
  translate(module, &globals);

  // TODO: Run the program too

  return 0;
}
