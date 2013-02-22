
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
        stack_offset = frame_size + 4 + arg->getArgNo() * 4;
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

  void spill(int reg, llvm::Instruction *inst) {
    int stack_offset = stackslots[inst];
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
  int frame_size;
};

enum {
  REG_EAX = 0,
  REG_ECX,
  REG_EDX,
  REG_EBX,
};

void translate(llvm::Module *module, std::map<std::string,uintptr_t> *funcs) {
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
      codebuf.stackslots[inst] = offset;
      offset += 4; // XXX: fixed size
    }
    codebuf.frame_size = offset;
    // Prolog:
    // subl $frame_size, %esp
    codebuf.put_byte(0x81);
    codebuf.put_byte(0xec);
    codebuf.put_uint32(codebuf.frame_size);

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
            assert(!"Unknown binary operator");
        }
        codebuf.spill(REG_ECX, inst);
      } else if (llvm::CmpInst *op = llvm::dyn_cast<llvm::CmpInst>(inst)) {
        codebuf.move_to_reg(REG_EAX, inst->getOperand(0));
        codebuf.move_to_reg(REG_ECX, inst->getOperand(1));
        switch (op->getPredicate()) {
          case llvm::CmpInst::ICMP_EQ:
            codebuf.put_code("\x31\xd2", 2); // xor %edx, %edx
            // cmp %eax, %ecx
            codebuf.put_byte(0x39);
            codebuf.put_byte(0xc1);
            codebuf.put_code("\x0f\x94\xc2", 3); // sete %dl
            break;
          default:
            assert(!"Unknown comparison");
        }
        codebuf.spill(REG_EDX, inst);
      } else if (llvm::LoadInst *op = llvm::dyn_cast<llvm::LoadInst>(inst)) {
        printf("load\n");
        codebuf.move_to_reg(REG_EAX, op->getPointerOperand());
        // movl (%eax), %eax
        codebuf.put_byte(0x8b);
        codebuf.put_byte(0x00);
        codebuf.spill(REG_EAX, inst);
      } else if (llvm::StoreInst *op = llvm::dyn_cast<llvm::StoreInst>(inst)) {
        printf("store\n");
        codebuf.move_to_reg(REG_EAX, op->getPointerOperand());
        codebuf.move_to_reg(REG_ECX, op->getValueOperand());
        // movl %ecx, (%eax)
        codebuf.put_byte(0x89);
        codebuf.put_byte(0x08);
        codebuf.spill(REG_EAX, inst);
      } else if (llvm::ReturnInst *op
                 = llvm::dyn_cast<llvm::ReturnInst>(inst)) {
        if (llvm::Value *result = op->getReturnValue())
          codebuf.move_to_reg(REG_EAX, result);
        printf("ret\n");
        // Epilog:
        // addl $frame_size, %esp
        codebuf.put_byte(0x81);
        codebuf.put_byte(0xc4);
        codebuf.put_uint32(codebuf.frame_size);
        codebuf.put_ret();
      } else {
        assert(!"Unknown instruction type");
      }
    }

    fflush(stdout);
    codebuf.dump();

    (*funcs)[func->getName()] = (uintptr_t) codebuf.get_start();
  }
}

void my_assert(int val1, int val2, const char *expr1, const char *expr2,
               const char *file, int line_number) {
  fprintf(stderr, "%i != %i: %s != %s at %s:%i\n",
          val1, val2, expr1, expr2, file, line_number);
  abort();
}

#define ASSERT_EQ(val1, val2)                                           \
  do {                                                                  \
    int _val1 = (val1);                                                 \
    int _val2 = (val2);                                                 \
    if (_val1 != _val2)                                                 \
      my_assert(_val1, _val2, #val1, #val2, __FILE__, __LINE__);        \
  } while (0);

int main() {
  llvm::SMDiagnostic err;
  llvm::LLVMContext &context = llvm::getGlobalContext();
  const char *filename = "test.ll";
  llvm::Module *module = llvm::ParseIRFile(filename, err, context);
  if (!module) {
    fprintf(stderr, "failed to read file: %s\n", filename);
    return 1;
  }

  std::map<std::string,uintptr_t> funcs;
  translate(module, &funcs);

  int (*func)(int arg);

  func = (typeof(func))(funcs["test_return"]);
  assert(func(0) == 123);

  func = (typeof(func))(funcs["test_add"]);
  assert(func(99) == 199);

  func = (typeof(func))(funcs["test_sub"]);
  assert(func(200) == 800);

  {
    int (*funcp)(int *ptr);
    funcp = (typeof(funcp))(funcs["test_load_int32"]);
    int value = 0x12345678;
    int cell = value;
    assert(funcp(&cell) == value);
  }

  {
    void (*funcp)(int *ptr, int value);
    funcp = (typeof(funcp))(funcs["test_store_int32"]);
    int value = 0x12345678;
    int cell = 0;
    funcp(&cell, value);
    assert(cell == value);
  }

  func = (typeof(func))(funcs["test_compare"]);
  ASSERT_EQ(func(99), 1);
  ASSERT_EQ(func(98), 0);
  ASSERT_EQ(func(100), 0);

  printf("OK\n");
  return 0;
}