
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include <map>

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

  void dump() {
    FILE *fp = fopen("tmp_data", "w");
    assert(fp);
    fwrite(buf_, 1, current_ - buf_, fp);
    fclose(fp);
    system("objdump -D -b binary -m i386 tmp_data");
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
      printf("const %i\n", val);
      // movl $INT32, %reg
      put_byte(0xb8 | reg);
      put_uint32(val);
    } else {
      std::map<llvm::Value*,int>::iterator slot = stackslots.find(value);
      int stack_offset;
      if (slot == stackslots.end()) {
        llvm::Argument *arg = llvm::cast<llvm::Argument>(value);
        stack_offset = 4 + arg->getArgNo() * 4;
      } else {
        stack_offset = slot->second;
      }
      // movl stack_offset(%esp), %reg
      printf("unspill 0x%x\n", stack_offset);
      put_byte(0x8b);
      put_byte(0x84 | (reg << 3));
      put_byte(0x24);
      put_uint32(stack_offset);
    }
  }

  void spill(int reg, int stack_offset) {
    // movl %reg, stack_offset(%esp)
    put_byte(0x89);
    put_byte(0x84 | (reg << 3));
    put_byte(0x24);
    put_uint32(stack_offset);
  }

  void put_ret() {
    put_byte(0xc3);
  }

  // XXX: move somewhere better
  std::map<llvm::Value*,int> stackslots;
};

enum {
  REG_EAX = 0,
  REG_ECX,
  REG_EDX,
  REG_EBX,
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
    // codebuf.put_byte(0xcc); // int3 debug

    llvm::BasicBlock *bb = &func->getEntryBlock();
    int offset = 0;
    for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
         inst != bb->end();
         ++inst) {
      // XXX: using a "red zone"
      offset -= 4;
      codebuf.stackslots[inst] = offset;
      // offset += 4; // XXX: fixed size
    }

    for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
         inst != bb->end();
         ++inst) {
      printf("-- inst\n");
      if (llvm::BinaryOperator *op =
              llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        codebuf.move_to_reg(REG_ECX, inst->getOperand(0));
        codebuf.move_to_reg(REG_EAX, inst->getOperand(1));
        switch (op->getOpcode()) {
          case llvm::Instruction::Add: {
            printf("add\n");
            char code[2] = { 0x01, 0xc1 }; // addl %eax, %ecx
            codebuf.put_code(code, sizeof(code));
            break;
          }
          case llvm::Instruction::Sub: {
            printf("sub\n");
            char code[2] = { 0x29, 0xc1 }; // subl %eax, %ecx
            codebuf.put_code(code, sizeof(code));
            break;
          }
          default:
            assert(0);
        }
        int stack_offset = codebuf.stackslots[inst];
        codebuf.spill(REG_ECX, stack_offset);
      } else if (llvm::ReturnInst *op
                 = llvm::dyn_cast<llvm::ReturnInst>(inst)) {
        codebuf.move_to_reg(REG_EAX, op->getReturnValue());
        printf("ret\n");
        codebuf.put_ret();
      } else {
        assert(0);
      }
    }

    fflush(stdout);
    codebuf.dump();

    int (*func)(int arg);
    func = (typeof(func))(codebuf.get_start());
    printf("func() -> %i\n", func(200));
  }
  return 0;
}
