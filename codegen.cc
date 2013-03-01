
#include "codegen.h"

#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include <map>

#include <llvm/Constants.h>
#include <llvm/InstrTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Module.h>

// In LLVM 3.2, this becomes <llvm/DataLayout.h>
#include <llvm/Target/TargetData.h>

#include "expand_getelementptr.h"

#define TEMPL(string) string, (sizeof(string) - 1)

#define UNHANDLED_TYPE(val, type) \
    if (llvm::isa<type>(val)) assert(!"Unhandled type: " #type)

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
    // Note that some of the types below are handled by write_global().
    UNHANDLED_TYPE(val, llvm::BlockAddress);
    UNHANDLED_TYPE(val, llvm::ConstantAggregateZero);
    UNHANDLED_TYPE(val, llvm::ConstantArray);
    UNHANDLED_TYPE(val, llvm::ConstantDataSequential);
    UNHANDLED_TYPE(val, llvm::ConstantFP);
    UNHANDLED_TYPE(val, llvm::ConstantInt);
    UNHANDLED_TYPE(val, llvm::ConstantPointerNull);
    UNHANDLED_TYPE(val, llvm::ConstantStruct);
    UNHANDLED_TYPE(val, llvm::ConstantVector);
    UNHANDLED_TYPE(val, llvm::UndefValue);
    assert(!"Unknown constant type");
  }
}

enum X86ArithOpcode {
  X86ArithAdd = 0,
  X86ArithOr,
  X86ArithAdc,
  X86ArithSbb,
  X86ArithAnd,
  X86ArithSub,
  X86ArithXor,
  X86ArithCmp,
};

class CodeBuf {
  char *buf_;
  char *buf_end_;
  char *current_;

public:
  CodeBuf() {
    // TODO: Use an expandable buffer
    int size = 0x2000;
    buf_ = (char *) mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(buf_ != MAP_FAILED);
    buf_end_ = buf_ + size;
    current_ = buf_;
  }

  char *get_current_pos() {
    return current_;
  }

  char *put_alloc_space(size_t size) {
    char *alloced = current_;
    assert(current_ + size < buf_end_);
    current_ += size;
    return alloced;
  }

  void put_code(const char *data, size_t size) {
    memcpy(put_alloc_space(size), data, size);
  }

  void put_byte(uint8_t val) {
    *(uint8_t *) put_alloc_space(sizeof(val)) = val;
  }

  void put_uint32(uint32_t val) {
    *(uint32_t *) put_alloc_space(sizeof(val)) = val;
  }

  llvm::Value *get_aliased_value(llvm::Value *inst) {
    // TODO: We could cache this mapping so that we don't have to
    // chase down the reference chain each time an aliased value is
    // used.
    if (llvm::isa<llvm::BitCastInst>(inst) ||
        llvm::isa<llvm::TruncInst>(inst) ||
        llvm::isa<llvm::PtrToIntInst>(inst) ||
        llvm::isa<llvm::IntToPtrInst>(inst)) {
      // TODO: Handle PtrToIntInst/IntToPtrInst for non-pointer-sized ints
      if (llvm::PtrToIntInst *conv = llvm::dyn_cast<llvm::PtrToIntInst>(inst)) {
        llvm::Function *func = conv->getParent()->getParent();
        assert(inst->getType() == data_layout->getIntPtrType(
                   func->getParent()->getContext()));
      } else if (llvm::IntToPtrInst *conv =
                 llvm::dyn_cast<llvm::IntToPtrInst>(inst)) {
        llvm::Function *func = conv->getParent()->getParent();
        assert(conv->getOperand(0)->getType() ==
               data_layout->getIntPtrType(func->getParent()->getContext()));
      }
      return llvm::cast<llvm::Instruction>(inst)->getOperand(0);
    }
    return NULL;
  }

  void move_to_reg(int reg, llvm::Value *value) {
    while (llvm::Value *alias = get_aliased_value(value))
      value = alias;
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
    } else if (llvm::isa<llvm::Instruction>(value) ||
               llvm::isa<llvm::Argument>(value)) {
      assert(stackslots.count(value) == 1);
      read_reg_from_ebp_offset(reg, stackslots[value]);
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
    write_reg_to_ebp_offset(reg, stackslots[inst]);
  }

  void put_ret() {
    put_byte(0xc3);
  }

  void put_sized_opcode(llvm::Type *type, int opcode_base) {
    llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(type);
    int bits = inttype->getBitWidth();
    assert(bits == 8 || bits == 16 || bits == 32);
    if (bits == 16) {
      put_byte(0x66); // DATA16 prefix
    }
    if (bits == 8) {
      put_byte(opcode_base);
    } else {
      put_byte(opcode_base + 1);
    }
  }

  void put_modrm_reg_reg(int reg1, int reg2) {
    put_byte((3 << 6) | (reg2 << 3) | reg1);
  }

  void put_arith_reg_reg(X86ArithOpcode arith_opcode,
                         int dest_reg, int src_reg) {
    put_byte((arith_opcode << 3) | (0 << 1) | 1); // Opcode
    put_modrm_reg_reg(dest_reg, src_reg);
  }

  void extend_to_i32(int reg, bool sign_extend, int src_size) {
    if (src_size == 32)
      return;
    if (src_size == 1) {
      if (sign_extend) {
        // shll $31, %reg
        put_byte(0xc1);
        put_byte(0xe0 | reg);
        put_byte(0x1f);
        // sarl $31, %reg
        put_byte(0xc1);
        put_byte(0xf8 | reg);
        put_byte(0x1f);
      } else {
        // andl $1, %reg
        put_byte(0x83);
        put_byte(0xe0 | reg);
        put_byte(0x01);
      }
      return;
    }
    assert(src_size == 8 || src_size == 16);

    put_byte(0x0f); // First opcode
    if (sign_extend) {
      // movsx (in Intel syntax)
      if (src_size == 8) {
        put_byte(0xbe);
      } else {
        put_byte(0xbf);
      }
    } else {
      // movzx (in Intel syntax)
      if (src_size == 8) {
        put_byte(0xb6);
      } else {
        put_byte(0xb7);
      }
    }
    put_modrm_reg_reg(reg, reg);
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
  std::map<llvm::Value*,int> stackslots;
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
                      CodeBuf &codebuf,
                      int tmp_reg) {
  for (llvm::BasicBlock::InstListType::iterator inst = to_bb->begin();
       inst != to_bb->end();
       ++inst) {
    llvm::PHINode *phi = llvm::dyn_cast<llvm::PHINode>(inst);
    if (!phi)
      break;
    codebuf.move_to_reg(tmp_reg, phi->getIncomingValueForBlock(from_bb));
    codebuf.spill(tmp_reg, phi);
  }
}

void unconditional_jump(llvm::BasicBlock *from_bb,
                        llvm::BasicBlock *to_bb,
                        CodeBuf &codebuf) {
  handle_phi_nodes(from_bb, to_bb, codebuf, REG_EAX);
  // jmp <label> (32-bit)
  codebuf.put_byte(0xe9);
  codebuf.direct_jump_offset32(to_bb);
}

int get_args_stack_size(llvm::CallInst *call) {
  // Assume args are all 32-bit
  return call->getNumArgOperands() * 4;
}

const char *get_instruction_type(llvm::Instruction *inst) {
  switch (inst->getOpcode()) {
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    case llvm::Instruction::OPCODE: return #OPCODE;
#include "llvm/Instruction.def"
#undef HANDLE_INST
    default: return "<unknown-instruction>";
  }
}

void translate_instruction(llvm::Instruction *inst, CodeBuf &codebuf) {
  if (llvm::BinaryOperator *op =
      llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
    llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(op->getType());
    int bits = inttype->getBitWidth();

    codebuf.move_to_reg(REG_EAX, inst->getOperand(0));
    codebuf.move_to_reg(REG_ECX, inst->getOperand(1));
    switch (op->getOpcode()) {
      case llvm::Instruction::Add: {
        codebuf.put_arith_reg_reg(X86ArithAdd, REG_EAX, REG_ECX);
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::Sub: {
        codebuf.put_arith_reg_reg(X86ArithSub, REG_EAX, REG_ECX);
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::Mul: {
        // result = %eax * %ecx
        // %eax = (uint32_t) result
        // %edx = (uint32_t) (result >> 32) -- we ignore this
        char code[2] = { 0xf7, 0xe1 }; // mull %ecx
        codebuf.put_code(code, sizeof(code));
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::UDiv:
      case llvm::Instruction::URem: {
        codebuf.extend_to_i32(REG_EAX, false, bits);
        codebuf.extend_to_i32(REG_ECX, false, bits);
        codebuf.put_code(TEMPL("\x31\xd2")); // xorl %edx, %edx
        // %eax = ((%edx << 32) | %eax) / %ecx
        char code[2] = { 0xf7, 0xf1 }; // divl %ecx
        codebuf.put_code(code, sizeof(code));
        if (op->getOpcode() == llvm::Instruction::UDiv) {
          codebuf.spill(REG_EAX, inst);
        } else {
          codebuf.spill(REG_EDX, inst);
        }
        break;
      }
      case llvm::Instruction::SDiv:
      case llvm::Instruction::SRem: {
        // TODO: This should extend args first, but this needs a test.
        assert(bits == 32);
        // Fill %edx with sign bit of %eax
        codebuf.put_code(TEMPL("\x99")); // cltd (cdq in Intel syntax)
        // %eax = ((%edx << 32) | %eax) / %ecx
        char code[2] = { 0xf7, 0xf9 }; // idivl %ecx
        codebuf.put_code(code, sizeof(code));
        if (op->getOpcode() == llvm::Instruction::SDiv) {
          codebuf.spill(REG_EAX, inst);
        } else {
          codebuf.spill(REG_EDX, inst);
        }
        break;
      }
      case llvm::Instruction::And: {
        codebuf.put_arith_reg_reg(X86ArithAnd, REG_EAX, REG_ECX);
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::Or: {
        codebuf.put_arith_reg_reg(X86ArithOr, REG_EAX, REG_ECX);
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::Xor: {
        codebuf.put_arith_reg_reg(X86ArithXor, REG_EAX, REG_ECX);
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::Shl: {
        codebuf.put_code(TEMPL("\xd3\xe0")); // shl %cl, %eax
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::LShr: {
        // TODO: This should extend args first, but this needs a test.
        assert(bits == 32);
        codebuf.put_code(TEMPL("\xd3\xe8")); // shr %cl, %eax
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::AShr: {
        // TODO: This should extend args first, but this needs a test.
        assert(bits == 32);
        codebuf.put_code(TEMPL("\xd3\xf8")); // sar %cl, %eax
        codebuf.spill(REG_EAX, inst);
        break;
      }
      default:
        assert(!"Unknown binary operator");
    }
  } else if (llvm::CmpInst *op = llvm::dyn_cast<llvm::CmpInst>(inst)) {
    llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(
        op->getOperand(0)->getType());
    int bits = inttype->getBitWidth();

    codebuf.move_to_reg(REG_ECX, inst->getOperand(0));
    codebuf.move_to_reg(REG_EAX, inst->getOperand(1));
    codebuf.extend_to_i32(REG_EAX, op->isSigned(), bits);
    codebuf.extend_to_i32(REG_ECX, op->isSigned(), bits);
    int x86_cond;
    switch (op->getPredicate()) {
      case llvm::CmpInst::ICMP_EQ:
        x86_cond = 0x4; // 'e' (equal)
        break;
      case llvm::CmpInst::ICMP_NE:
        x86_cond = 0x5; // 'ne' (not equal)
        break;
      // Unsigned comparisons
      case llvm::CmpInst::ICMP_UGT:
        x86_cond = 0x7; // 'a' (above)
        break;
      case llvm::CmpInst::ICMP_UGE:
        x86_cond = 0x3; // 'ae' (above or equal)
        break;
      case llvm::CmpInst::ICMP_ULT:
        x86_cond = 0x2; // 'b' (below)
        break;
      case llvm::CmpInst::ICMP_ULE:
        x86_cond = 0x6; // 'be' (below or equal)
        break;
      // Signed comparisons
      case llvm::CmpInst::ICMP_SGT:
        x86_cond = 0xf; // 'g' (greater)
        break;
      case llvm::CmpInst::ICMP_SGE:
        x86_cond = 0xd; // 'ge' (greater or equal)
        break;
      case llvm::CmpInst::ICMP_SLT:
        x86_cond = 0xc; // 'l' (less)
        break;
      case llvm::CmpInst::ICMP_SLE:
        x86_cond = 0xe; // 'le' (less or equal)
        break;
      default:
        assert(!"Unknown comparison");
    }
    // XXX: we zero-extend first here
    codebuf.put_code("\x31\xd2", 2); // xor %edx, %edx
    // cmp %eax, %ecx
    codebuf.put_byte(0x39);
    codebuf.put_byte(0xc1);
    // XXX: could store directly in stack slot
    // setCC %dl
    codebuf.put_byte(0x0f);
    codebuf.put_byte(0x90 | x86_cond);
    codebuf.put_byte(0xc2);
    codebuf.spill(REG_EDX, inst);
  } else if (llvm::LoadInst *op = llvm::dyn_cast<llvm::LoadInst>(inst)) {
    codebuf.move_to_reg(REG_EAX, op->getPointerOperand());
    // mov<size> (%eax), %eax
    codebuf.put_sized_opcode(op->getType(), 0x8a);
    codebuf.put_byte(0x00);
    codebuf.spill(REG_EAX, inst);
  } else if (llvm::StoreInst *op = llvm::dyn_cast<llvm::StoreInst>(inst)) {
    codebuf.move_to_reg(REG_EAX, op->getPointerOperand());
    codebuf.move_to_reg(REG_ECX, op->getValueOperand());
    // mov<size> %ecx, (%eax)
    codebuf.put_sized_opcode(op->getValueOperand()->getType(), 0x88);
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
    llvm::BasicBlock *bb = inst->getParent();
    if (op->isConditional()) {
      handle_phi_nodes(bb, op->getSuccessor(0), codebuf, REG_EAX);
      codebuf.move_to_reg(REG_EAX, op->getCondition());
      codebuf.put_code(TEMPL("\x85\xc0")); // testl %eax, %eax
      codebuf.put_code(TEMPL("\x0f\x85")); // jnz <label> (32-bit)
      codebuf.direct_jump_offset32(op->getSuccessor(0));
      unconditional_jump(bb, op->getSuccessor(1), codebuf);
    } else {
      assert(op->isUnconditional());
      unconditional_jump(bb, op->getSuccessor(0), codebuf);
    }
  } else if (llvm::SwitchInst *op =
             llvm::dyn_cast<llvm::SwitchInst>(inst)) {
    llvm::BasicBlock *bb = inst->getParent();
    llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(
        op->getCondition()->getType());
    int bits = inttype->getBitWidth();

    codebuf.move_to_reg(REG_EAX, op->getCondition());
    codebuf.extend_to_i32(REG_EAX, false, bits);
    for (llvm::SwitchInst::CaseIt iter = op->case_begin();
         iter != op->case_end();
         ++iter) {
      handle_phi_nodes(bb, iter.getCaseSuccessor(), codebuf, REG_EDX);
      codebuf.move_to_reg(REG_ECX, iter.getCaseValue());
      // cmp %eax, %ecx
      codebuf.put_byte(0x39);
      codebuf.put_byte(0xc1);
      // je <label> (32-bit)
      codebuf.put_byte(0x0f);
      codebuf.put_byte(0x84);
      codebuf.direct_jump_offset32(iter.getCaseSuccessor());
    }
    unconditional_jump(bb, op->getDefaultDest(), codebuf);
  } else if (llvm::isa<llvm::PHINode>(inst)) {
    // Nothing to do: phi nodes are handled by branches.
    // XXX: Someone still needs to validate that phi nodes only
    // appear in the right places.
  } else if (llvm::isa<llvm::ZExtInst>(inst) ||
             llvm::isa<llvm::SExtInst>(inst)) {
    llvm::Value *arg = inst->getOperand(0);
    llvm::IntegerType *from_type =
      llvm::cast<llvm::IntegerType>(arg->getType());
    bool sign_extend = llvm::dyn_cast<llvm::SExtInst>(inst);
    codebuf.move_to_reg(REG_EAX, arg);
    codebuf.extend_to_i32(REG_EAX, sign_extend, from_type->getBitWidth());
    codebuf.spill(REG_EAX, inst);
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
    int size = codebuf.data_layout->getTypeAllocSize(op->getAllocatedType());
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
      // Optimization.
      codebuf.spill(REG_ESP, op);
    }
  } else if (codebuf.get_aliased_value(inst)) {
    // Nothing to do:  handled elsewhere.
  } else {
    fprintf(stderr, "Unknown instruction type: %s\n",
            get_instruction_type(inst));
    assert(0);
  }
}

void translate_bb(llvm::BasicBlock *bb, CodeBuf &codebuf) {
  codebuf.make_label(bb);
  for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
       inst != bb->end();
       ++inst) {
    translate_instruction(inst, codebuf);
  }
}

void write_global(CodeBuf *codebuf, DataSegment *dataseg,
                  llvm::Constant *init) {
  if (llvm::ConstantInt *val = llvm::dyn_cast<llvm::ConstantInt>(init)) {
    assert(val->getBitWidth() % 8 == 0);
    size_t size = val->getBitWidth() / 8;
    // Assumes little endian.
    memcpy(dataseg->current, val->getValue().getRawData(), size);
    dataseg->current += size;
    assert(codebuf->data_layout->getTypeAllocSize(init->getType()) == size);
  } else if (llvm::isa<llvm::ConstantAggregateZero>(init) ||
             llvm::isa<llvm::ConstantPointerNull>(init)) {
    dataseg->current += codebuf->data_layout->getTypeAllocSize(init->getType());
  } else if (llvm::ConstantArray *val =
             llvm::dyn_cast<llvm::ConstantArray>(init)) {
    for (unsigned i = 0; i < val->getNumOperands(); ++i) {
      write_global(codebuf, dataseg, val->getOperand(i));
    }
  } else if (llvm::ConstantDataSequential *val =
             llvm::dyn_cast<llvm::ConstantDataSequential>(init)) {
    // Note that getRawDataValues() assumes the host endianness is the same.
    llvm::StringRef str = val->getRawDataValues();
    memcpy(dataseg->current, str.data(), str.size());
    dataseg->current += str.size();
  } else if (llvm::ConstantStruct *val =
             llvm::dyn_cast<llvm::ConstantStruct>(init)) {
    const llvm::StructLayout *layout = codebuf->data_layout->getStructLayout(
        val->getType());
    uint64_t prev_offset = 0;
    for (unsigned i = 0; i < val->getNumOperands(); ++i) {
      llvm::Constant *field = val->getOperand(i);
      write_global(codebuf, dataseg, field);

      // Add padding.
      uint64_t next_offset =
        i == val->getNumOperands() - 1
        ? codebuf->data_layout->getTypeAllocSize(init->getType())
        : layout->getElementOffset(i + 1);
      uint64_t field_size =
        codebuf->data_layout->getTypeAllocSize(field->getType());
      uint64_t padding_size = next_offset - prev_offset - field_size;
      dataseg->current += padding_size;
      prev_offset = next_offset;
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

void translate_function(llvm::Function *func, CodeBuf &codebuf) {
  llvm::BasicBlockPass *expand_gep = createExpandGetElementPtrPass();
  int callees_args_size = 0;
  for (llvm::Function::iterator bb = func->begin();
       bb != func->end();
       ++bb) {
    expand_gep->runOnBasicBlock(*bb);
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

  for (llvm::Function::ArgumentListType::iterator arg = func->arg_begin();
       arg != func->arg_end();
       ++arg) {
    // XXX: We assume arguments are int32s
    int offset = 8; // Skip return address and frame pointer
    assert(codebuf.stackslots.count(arg) == 0);
    codebuf.stackslots[arg] = offset + arg->getArgNo() * 4;
  }

  int vars_size = 0;
  for (llvm::Function::iterator bb = func->begin();
       bb != func->end();
       ++bb) {
    for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
         inst != bb->end();
         ++inst) {
      assert(codebuf.stackslots.count(inst) == 0);
      if (!codebuf.get_aliased_value(inst)) {
        // XXX: We assume variables are int32s
        vars_size += 4;
        codebuf.stackslots[inst] = -vars_size;
      }
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
    translate_bb(bb, codebuf);
  }

  printf("%s:\n", func->getName().data());
  fflush(stdout);
  dump_range_as_code(function_entry, codebuf.get_current_pos());

  codebuf.globals[func] = (uintptr_t) function_entry;

  delete expand_gep;
}

void translate(llvm::Module *module, std::map<std::string,uintptr_t> *globals) {
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
    translate_function(func, codebuf);
  }
  codebuf.apply_jump_relocs();
  codebuf.apply_global_relocs();

  for (std::map<llvm::GlobalValue*,uint32_t>::iterator global =
         codebuf.globals.begin();
       global != codebuf.globals.end();
       ++global) {
    (*globals)[global->first->getName()] = global->second;
  }
}
