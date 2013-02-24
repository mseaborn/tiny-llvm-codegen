
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

// In LLVM 3.2, this becomes <llvm/DataLayout.h>
#include <llvm/Target/TargetData.h>

#define TEMPL(string) string, (sizeof(string) - 1)

void dump_range_as_code(char *start, char *end) {
  FILE *fp = fopen("tmp_data", "w");
  assert(fp);
  fwrite(start, 1, end - start, fp);
  fclose(fp);
  system("objdump -D -b binary -m i386 tmp_data | grep '^ '");
}

void expand_constant(llvm::Constant *val, llvm::TargetData *data_layout,
                     llvm::GlobalValue **result_global, int *result_offset) {
  if (llvm::GlobalValue *global = llvm::dyn_cast<llvm::GlobalValue>(val)) {
    *result_global = global;
    *result_offset = 0;
  } else if (llvm::ConstantExpr *expr =
             llvm::dyn_cast<llvm::ConstantExpr>(val)) {
    if (expr->getOpcode() == llvm::Instruction::GetElementPtr) {
      expand_constant(expr->getOperand(0), data_layout,
                      result_global, result_offset);
      llvm::SmallVector<llvm::Value*,8> indexes(expr->op_begin() + 1,
                                                expr->op_end());
      *result_offset += data_layout->getIndexedOffset(
          expr->getOperand(0)->getType(), indexes);
    } else {
      assert(!"Unknown ConstantExpr");
    }
  } else {
    assert(!"Unknown constant type");
  }
}

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

  char *get_current_pos() {
    return current_;
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
    } else if (llvm::Constant *cval = llvm::dyn_cast<llvm::Constant>(value)) {
      llvm::GlobalValue *global;
      int offset;
      expand_constant(cval, data_layout, &global, &offset);
      // movl $INT32, %reg
      put_byte(0xb8 | reg);
      put_global_reloc(global, offset);
    } else if (llvm::Argument *arg = llvm::dyn_cast<llvm::Argument>(value)) {
      int offset = 8; // Skip return address and frame pointer
      read_reg_from_ebp_offset(reg, offset + arg->getArgNo() * 4);
    } else if (llvm::Instruction *inst =
               llvm::dyn_cast<llvm::Instruction>(value)) {
      assert(stackslots.count(inst) == 1);
      read_reg_from_ebp_offset(reg, -stackslots[inst]);
    } else {
      assert(!"Unknown value type");
    }
  }

  void read_reg_from_ebp_offset(int reg, int ebp_offset) {
    // movl ebp_offset(%ebp), %reg
    put_byte(0x8b);
    put_byte(0x85 | (reg << 3));
    put_uint32(ebp_offset);
    // Omit-frame-pointer version:
    // // movl stack_offset(%esp), %reg
    // put_byte(0x8b);
    // put_byte(0x84 | (reg << 3));
    // put_byte(0x24);
    // put_uint32(stack_offset);
  }

  void write_reg_to_ebp_offset(int reg, int stack_offset) {
    // movl %reg, stack_offset(%ebp)
    put_byte(0x89);
    put_byte(0x85 | (reg << 3));
    put_uint32(stack_offset);
  }

  void write_reg_to_esp_offset(int reg, int stack_offset) {
    // movl %reg, stack_offset(%esp)
    put_byte(0x89);
    put_byte(0x84 | (reg << 3));
    put_byte(0x24);
    put_uint32(stack_offset);
  }

  void spill(int reg, llvm::Instruction *inst) {
    write_reg_to_ebp_offset(reg, -stackslots[inst]);
  }

  void put_ret() {
    put_byte(0xc3);
  }

  void make_label(llvm::BasicBlock *bb) {
    assert(labels.count(bb) == 0);
    labels[bb] = (uint32_t) current_;
  }

  void direct_jump_offset32(llvm::BasicBlock *dest) {
    put_uint32(0); // Placeholder
    jump_relocs.push_back(JumpReloc((uint32_t *) current_, dest));
  }

  void put_global_reloc(llvm::GlobalValue *dest, int offset) {
    global_relocs.push_back(GlobalReloc((uint32_t *) current_, dest));
    put_uint32(offset);
  }

  void apply_jump_relocs() {
    for (std::vector<JumpReloc>::iterator reloc = jump_relocs.begin();
         reloc != jump_relocs.end();
         ++reloc) {
      assert(labels.count(reloc->second) == 1);
      uint32_t target = labels[reloc->second];
      uint32_t *jump_loc = reloc->first;
      jump_loc[-1] = target - (uint32_t) jump_loc;
    }
  }

  void apply_global_relocs() {
    for (std::vector<GlobalReloc>::iterator reloc = global_relocs.begin();
         reloc != global_relocs.end();
         ++reloc) {
      assert(globals.count(reloc->second) == 1);
      uint32_t value = globals[reloc->second];
      uint32_t *addr = reloc->first;
      *addr += value;
    }
  }

  // XXX: move somewhere better
  std::map<llvm::Instruction*,int> stackslots;
  std::map<llvm::BasicBlock*,uint32_t> labels;
  std::map<llvm::GlobalValue*,uint32_t> globals;
  int frame_vars_size;
  int frame_callees_args_size;

  llvm::TargetData *data_layout;

  typedef std::pair<uint32_t*,llvm::BasicBlock*> JumpReloc;
  std::vector<JumpReloc> jump_relocs;

  typedef std::pair<uint32_t*,llvm::GlobalValue*> GlobalReloc;
  std::vector<GlobalReloc> global_relocs;
};

class DataSegment {
  char *buf_;

public:
  DataSegment() {
    buf_ = (char *) mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(buf_ != MAP_FAILED);
    current = buf_;
  }

  char *current;
};

enum {
  REG_EAX = 0,
  REG_ECX,
  REG_EDX,
  REG_EBX,
  REG_ESP,
  REG_EBP,
  REG_ESI,
  REG_EDI,
};

void handle_phi_nodes(llvm::BasicBlock *from_bb,
                      llvm::BasicBlock *to_bb,
                      CodeBuf &codebuf) {
  for (llvm::BasicBlock::InstListType::iterator inst = to_bb->begin();
       inst != to_bb->end();
       ++inst) {
    llvm::PHINode *phi = llvm::dyn_cast<llvm::PHINode>(inst);
    if (!phi)
      break;
    codebuf.move_to_reg(REG_EAX, phi->getIncomingValueForBlock(from_bb));
    codebuf.spill(REG_EAX, phi);
  }
}

void unconditional_jump(llvm::BasicBlock *from_bb,
                        llvm::BasicBlock *to_bb,
                        CodeBuf &codebuf) {
  handle_phi_nodes(from_bb, to_bb, codebuf);
  // jmp <label> (32-bit)
  codebuf.put_byte(0xe9);
  codebuf.direct_jump_offset32(to_bb);
}

int get_args_stack_size(llvm::CallInst *call) {
  // Assume args are all 32-bit
  return call->getNumArgOperands() * 4;
}

void translate_bb(llvm::BasicBlock *bb, CodeBuf &codebuf,
                  llvm::TargetData &data_layout) {
  codebuf.make_label(bb);
  for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
       inst != bb->end();
       ++inst) {
    if (llvm::BinaryOperator *op =
        llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
      codebuf.move_to_reg(REG_ECX, inst->getOperand(0));
      codebuf.move_to_reg(REG_EAX, inst->getOperand(1));
      switch (op->getOpcode()) {
        case llvm::Instruction::Add: {
          char code[2] = { 0x01, 0xc1 }; // addl %eax, %ecx
          codebuf.put_code(code, sizeof(code));
          break;
        }
        case llvm::Instruction::Sub: {
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
          // XXX: we zero-extend first here
          codebuf.put_code("\x31\xd2", 2); // xor %edx, %edx
          // cmp %eax, %ecx
          codebuf.put_byte(0x39);
          codebuf.put_byte(0xc1);
          // XXX: could store directly in stack slot
          codebuf.put_code("\x0f\x94\xc2", 3); // sete %dl
          break;
        default:
          assert(!"Unknown comparison");
      }
      codebuf.spill(REG_EDX, inst);
    } else if (llvm::LoadInst *op = llvm::dyn_cast<llvm::LoadInst>(inst)) {
      codebuf.move_to_reg(REG_EAX, op->getPointerOperand());
      // movl (%eax), %eax
      codebuf.put_byte(0x8b);
      codebuf.put_byte(0x00);
      codebuf.spill(REG_EAX, inst);
    } else if (llvm::StoreInst *op = llvm::dyn_cast<llvm::StoreInst>(inst)) {
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
      // Epilog:
      codebuf.put_byte(0xc9); // leave
      // Omit-frame-pointer version:
      // // addl $frame_size, %esp
      // codebuf.put_byte(0x81);
      // codebuf.put_byte(0xc4);
      // codebuf.put_uint32(codebuf.frame_size);
      codebuf.put_ret();
    } else if (llvm::BranchInst *op =
               llvm::dyn_cast<llvm::BranchInst>(inst)) {
      // TODO: could implement fallthrough to next basic block
      if (op->isConditional()) {
        codebuf.move_to_reg(REG_EAX, op->getCondition());
        handle_phi_nodes(bb, op->getSuccessor(0), codebuf);
        codebuf.put_code(TEMPL("\x85\xc0")); // testl %eax, %eax
        codebuf.put_code(TEMPL("\x0f\x85")); // jnz <label> (32-bit)
        codebuf.direct_jump_offset32(op->getSuccessor(0));
        unconditional_jump(bb, op->getSuccessor(1), codebuf);
      } else {
        assert(op->isUnconditional());
        unconditional_jump(bb, op->getSuccessor(0), codebuf);
      }
    } else if (llvm::PHINode *op = llvm::dyn_cast<llvm::PHINode>(inst)) {
      // Nothing to do: phi nodes are handled by branches.
      // XXX: Someone still needs to validate that phi nodes only
      // appear in the right places.
    } else if (llvm::CallInst *op = llvm::dyn_cast<llvm::CallInst>(inst)) {
      // We have already reserved space on the stack to store our
      // callee's argument.
      for (unsigned i = 0; i < op->getNumArgOperands(); ++i) {
        codebuf.move_to_reg(REG_EAX, op->getArgOperand(i));
        // Assume args are all 32-bit
        codebuf.write_reg_to_esp_offset(REG_EAX, i * 4);
      }
      // TODO: Optimise to output direct calls too
      codebuf.move_to_reg(REG_EAX, op->getCalledValue());
      codebuf.put_code(TEMPL("\xff\xd0")); // call *%eax
      codebuf.spill(REG_EAX, op);
    } else if (llvm::AllocaInst *op = llvm::dyn_cast<llvm::AllocaInst>(inst)) {
      // XXX: Handle alignment
      // XXX: Handle variable sizes
      assert(!op->isArrayAllocation());
      int size = data_layout.getTypeAllocSize(op->getAllocatedType());
      // subl $size, %esp
      codebuf.put_byte(0x81);
      codebuf.put_byte(0xec);
      codebuf.put_uint32(size);
      if (codebuf.frame_callees_args_size != 0) {
        // leal OFFSET(%esp), %eax
        codebuf.put_code(TEMPL("\x8d\x84\x24"));
        codebuf.put_uint32(codebuf.frame_callees_args_size);
        codebuf.spill(REG_EAX, op);
      } else {
        codebuf.spill(REG_ESP, op);
      }
    } else {
      assert(!"Unknown instruction type");
    }
  }
}

void write_global(CodeBuf *codebuf, DataSegment *dataseg,
                  llvm::Constant *init) {
  if (llvm::ConstantInt *val = llvm::dyn_cast<llvm::ConstantInt>(init)) {
    assert(val->getBitWidth() % 8 == 0);
    int size = val->getBitWidth() / 8;
    // Assumes little endian.
    memcpy(dataseg->current, val->getValue().getRawData(), size);
    dataseg->current += size;
  } else if (llvm::ConstantArray *val =
             llvm::dyn_cast<llvm::ConstantArray>(init)) {
    for (unsigned i = 0; i < val->getNumOperands(); ++i) {
      write_global(codebuf, dataseg, val->getOperand(i));
    }
  } else {
    // TODO: unify fully with expand_constant().
    llvm::GlobalValue *global;
    int offset;
    expand_constant(init, codebuf->data_layout, &global, &offset);
    assert(global);
    // This mirrors put_global_reloc().
    // TODO: unify these.
    codebuf->global_relocs.push_back(
        CodeBuf::GlobalReloc((uint32_t *) dataseg->current, global));
    assert(codebuf->data_layout->getTypeAllocSize(init->getType())
           == sizeof(uint32_t));
    *(uint32_t *) dataseg->current = offset;
    dataseg->current += sizeof(uint32_t);
  }
}

void translate(llvm::Module *module, std::map<std::string,uintptr_t> *funcs) {
  CodeBuf codebuf;
  DataSegment dataseg;

  llvm::TargetData data_layout(module);
  codebuf.data_layout = &data_layout;

  for (llvm::Module::GlobalListType::iterator global = module->global_begin();
       global != module->global_end();
       ++global) {
    // TODO: handle alignments
    uint32_t addr = (uint32_t) dataseg.current;
    size_t size =
      data_layout.getTypeAllocSize(global->getType()->getElementType());
    codebuf.globals[global] = (uint32_t) dataseg.current;
    if (llvm::Constant *init = global->getInitializer()) {
      write_global(&codebuf, &dataseg, init);
      assert(dataseg.current == (char *) addr + size);
    } else {
      dataseg.current += size;
    }
  }

  for (llvm::Module::FunctionListType::iterator func = module->begin();
       func != module->end();
       ++func) {
    int callees_args_size = 0;
    for (llvm::Function::iterator bb = func->begin();
         bb != func->end();
         ++bb) {
      for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
           inst != bb->end();
           ++inst) {
        if (llvm::CallInst *call = llvm::dyn_cast<llvm::CallInst>(inst)) {
          callees_args_size =
            std::max(callees_args_size, get_args_stack_size(call));
        }
      }
    }
    codebuf.frame_callees_args_size = callees_args_size;

    int vars_size = 0;
    for (llvm::Function::iterator bb = func->begin();
         bb != func->end();
         ++bb) {
      for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
           inst != bb->end();
           ++inst) {
        // XXX: We assume variables are int32s
        vars_size += 4;
        codebuf.stackslots[inst] = vars_size;
      }
    }
    codebuf.frame_vars_size = vars_size;
    int frame_size = codebuf.frame_vars_size + codebuf.frame_callees_args_size;

    char *function_entry = codebuf.get_current_pos();
    // Prolog:
    codebuf.put_byte(0x55); // pushl %ebp
    codebuf.put_code(TEMPL("\x89\xe5")); // movl %esp, %ebp
    // subl $frame_size, %esp
    codebuf.put_byte(0x81);
    codebuf.put_byte(0xec);
    codebuf.put_uint32(frame_size);

    for (llvm::Function::iterator bb = func->begin();
         bb != func->end();
         ++bb) {
      translate_bb(bb, codebuf, data_layout);
    }

    printf("%s:\n", func->getName().data());
    fflush(stdout);
    dump_range_as_code(function_entry, codebuf.get_current_pos());

    codebuf.globals[func] = (uintptr_t) function_entry;
    (*funcs)[func->getName()] = (uintptr_t) function_entry;
  }
  codebuf.apply_jump_relocs();
  codebuf.apply_global_relocs();
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

int sub_func(int x, int y) {
  printf("sub_func(%i, %i) called\n", x, y);
  return x - y;
}

#define GET_FUNC(func, name) \
    printf("testing %s\n", name); \
    func = (typeof(func)) (funcs[name]); \
    assert(func);

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

  GET_FUNC(func, "test_return");
  assert(func(0) == 123);

  GET_FUNC(func, "test_add");
  assert(func(99) == 199);

  GET_FUNC(func, "test_sub");
  assert(func(200) == 800);

  {
    int (*funcp)(int *ptr);
    GET_FUNC(funcp, "test_load_int32");
    int value = 0x12345678;
    int cell = value;
    assert(funcp(&cell) == value);
  }

  {
    void (*funcp)(int *ptr, int value);
    GET_FUNC(funcp, "test_store_int32");
    int value = 0x12345678;
    int cell = 0;
    funcp(&cell, value);
    assert(cell == value);
  }

  GET_FUNC(func, "test_compare");
  ASSERT_EQ(func(99), 1);
  ASSERT_EQ(func(98), 0);
  ASSERT_EQ(func(100), 0);

  GET_FUNC(func, "test_branch");
  ASSERT_EQ(func(0), 101);

  GET_FUNC(func, "test_conditional");
  ASSERT_EQ(func(99), 123);
  ASSERT_EQ(func(98), 456);

  GET_FUNC(func, "test_phi");
  ASSERT_EQ(func(99), 123);
  ASSERT_EQ(func(98), 456);

  {
    int (*funcp)(int (*func)(int arg1, int arg2), int arg1, int arg2);
    GET_FUNC(funcp, "test_call");
    ASSERT_EQ(funcp(sub_func, 50, 10), 1040);

    GET_FUNC(funcp, "test_call2");
    ASSERT_EQ(funcp(sub_func, 50, 10), 40);
  }

  {
    int (*funcp)(void);
    GET_FUNC(funcp, "test_direct_call");
    ASSERT_EQ(funcp(), 123);
  }

  {
    int *(*funcp)(void);
    GET_FUNC(funcp, "get_global");
    int *ptr = funcp();
    assert(ptr);
    ASSERT_EQ(*ptr, 124);
  }

  {
    char *(*funcp)(void);
    GET_FUNC(funcp, "get_global_string");
    char *str = funcp();
    assert(str);
    ASSERT_EQ(strcmp(str, "Hello!"), 0);
  }

  {
    short *(*funcp)(void);
    GET_FUNC(funcp, "get_global_array");
    short *array = funcp();
    ASSERT_EQ(array[0], 6);
    ASSERT_EQ(array[-1], 5);
  }

  {
    int (*funcp)(void);
    GET_FUNC(funcp, "test_alloca");
    ASSERT_EQ(funcp(), 125);

    GET_FUNC(funcp, "test_alloca2");
    ASSERT_EQ(funcp(), 125);
  }

  printf("OK\n");
  return 0;
}
