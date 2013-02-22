
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include <llvm/Constants.h>
#include <llvm/InstrTypes.h>
#include <llvm/Instructions.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/IRReader.h>

class CodeBuf {
  char *buf_;
  char *current_;

public:
  CodeBuf() {
    buf_ = (char *) mmap(NULL, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(buf_ != MAP_FAILED);
    current_ = buf_;
  }

  char *get_start() {
    return buf_;
  }

  void put_code(const char *data, size_t size) {
    memcpy(current_, data, size);
    current_ += size;
  }

  void put_byte(uint8_t val) {
    memcpy(current_, &val, sizeof(val));
    current_ += sizeof(uint8_t);
  }

  void put_uint32(uint32_t val) {
    memcpy(current_, &val, sizeof(val));
    current_ += sizeof(val);
  }

  void move_to_reg(int reg, llvm::Value *value) {
    if (llvm::ConstantInt *cval = llvm::dyn_cast<llvm::ConstantInt>(value)) {
      // XXX: truncates
      uint32_t val = cval->getLimitedValue();
      // movl $INT32, %reg
      put_byte(0xb8 | reg);
      put_uint32(val);
    }
  }

  void put_ret() {
    put_byte(0xc3);
  }
};

enum {
  REG_EAX = 0
};

int main() {
  llvm::SMDiagnostic err;
  llvm::LLVMContext &context = llvm::getGlobalContext();
  const char *filename = "test.ll";
  llvm::Module *module = llvm::ParseIRFile(filename, err, context);
  if (!module) {
    fprintf(stderr, "failed to read file: %s\n", filename);
    return 1;
  }

  for (llvm::Module::FunctionListType::iterator func = module->begin();
       func != module->end();
       ++func) {
    printf("got func\n");
    CodeBuf codebuf;

    llvm::BasicBlock *bb = &func->getEntryBlock();
    for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
         inst != bb->end();
         ++inst) {
      if (llvm::BinaryOperator *op =
              llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        printf("binop inst\n");
        assert(0);
      } else if (llvm::ReturnInst *op
                 = llvm::dyn_cast<llvm::ReturnInst>(inst)) {
        codebuf.move_to_reg(REG_EAX, op->getReturnValue());
        codebuf.put_ret();
      } else {
        assert(0);
      }
      printf("inst\n");
    }

    int (*func)();
    func = (typeof(func))(codebuf.get_start());
    printf("func() -> %i\n", func());
  }
  return 0;
}
