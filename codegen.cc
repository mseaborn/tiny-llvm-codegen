//===- codegen.cc - Fast and stupid code generator-------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "codegen.h"

#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include <map>

#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>

#include "expand_constantexpr.h"
#include "expand_getelementptr.h"
#include "expand_varargs.h"
#include "gen_runtime_helpers_atomic.h"
#include "runtime_helpers.h"

#define TEMPL(string) string, (sizeof(string) - 1)

#define UNHANDLED_TYPE(val, type) \
    if (llvm::isa<type>(val)) assert(!"Unhandled type: " #type)

static const int kPointerSizeBits = 32;

// We always reserve stack space for calling runtime helper functions.
// TODO: Only reserve this stack space if it is actually needed.
static const int kMinCalleeArgsSize = 4 * 3; // 3 arguments

void dump_range_as_code(char *start, char *end) {
  FILE *fp = fopen("tmp_data", "w");
  assert(fp);
  fwrite(start, 1, end - start, fp);
  fclose(fp);
  system("objdump -D -b binary -m i386 tmp_data | grep '^ '");
}

void runtime_log(const char *msg) {
  fprintf(stderr, "%s\n", msg);
}

// TODO: Remove all uses of unhandled_case()!
void runtime_unhandled(const char *desc) {
  fprintf(stderr, "Runtime fatal error: case not handled: %s\n", desc);
  abort();
}

bool is_i64(llvm::Type *ty) {
  if (llvm::IntegerType *intty = llvm::dyn_cast<llvm::IntegerType>(ty)) {
    int bits = intty->getBitWidth();
    if (bits > 32)
      assert(bits == 64);
    return bits == 64;
  }
  return false;
}

void expand_constant(llvm::Constant *val, llvm::DataLayout *data_layout,
                     llvm::GlobalValue **result_global,
                     uint64_t *result_offset,
                     const char **result_unhandled) {
  if (llvm::GlobalValue *global = llvm::dyn_cast<llvm::GlobalValue>(val)) {
    *result_global = global;
    *result_offset = 0;
  } else if (llvm::ConstantInt *cval = llvm::dyn_cast<llvm::ConstantInt>(val)) {
    *result_global = NULL;
    *result_offset = cval->getZExtValue();
    // Check for possible truncation.
    assert(*result_offset == cval->getZExtValue());
  } else if (llvm::ConstantFP *cval = llvm::dyn_cast<llvm::ConstantFP>(val)) {
    llvm::APInt data = cval->getValueAPF().bitcastToAPInt();
    assert(data.getBitWidth() == 64);
    *result_global = NULL;
    *result_offset = data.getZExtValue();
  } else if (llvm::isa<llvm::ConstantPointerNull>(val)) {
    *result_global = NULL;
    *result_offset = 0;
  } else if (llvm::isa<llvm::UndefValue>(val)) {
    // Note that UndefValue could be handled in other places to
    // optimise so that a variable is left uninitialized, although
    // this will make the generated code behave less predictably.
    *result_global = NULL;
    *result_offset = 0;
  } else if (llvm::ConstantExpr *expr =
             llvm::dyn_cast<llvm::ConstantExpr>(val)) {
    if (expr->getOpcode() == llvm::Instruction::GetElementPtr) {
      expand_constant(expr->getOperand(0), data_layout,
                      result_global, result_offset, result_unhandled);
      llvm::SmallVector<llvm::Value*,8> indexes(expr->op_begin() + 1,
                                                expr->op_end());
      *result_offset += data_layout->getIndexedOffset(
          expr->getOperand(0)->getType(), indexes);
    } else if (expr->getOpcode() == llvm::Instruction::BitCast ||
               expr->getOpcode() == llvm::Instruction::PtrToInt ||
               expr->getOpcode() == llvm::Instruction::IntToPtr) {
      // TODO: Do we need to truncate if a 64-bit constant is cast to
      // a pointer and back again?
      expand_constant(expr->getOperand(0), data_layout,
                      result_global, result_offset, result_unhandled);
    } else {
      // TODO: Handle remaining cases.
      *result_unhandled = "Unknown ConstantExpr";
      *result_global = NULL;
      *result_offset = 0;
    }
  } else {
    // Note that some of the types below are handled by write_global().
    UNHANDLED_TYPE(val, llvm::BlockAddress);
    UNHANDLED_TYPE(val, llvm::ConstantAggregateZero);
    UNHANDLED_TYPE(val, llvm::ConstantArray);
    UNHANDLED_TYPE(val, llvm::ConstantDataSequential);
    UNHANDLED_TYPE(val, llvm::ConstantFP);
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

class DataBuffer {
  char *buf_;
  char *buf_end_;
  char *current_;

public:
  DataBuffer(int prot) {
    // TODO: Use an expandable buffer.
    // For now, allocating a large buffer means that we know the
    // absolute address of a global variable (for example) at the
    // point we generate it, before we finish generating all code and
    // data.  This means we don't need to implement full relocations
    // yet.
    int size = 16 * 1024 * 1024; // 16MB
    buf_ = (char *) mmap(NULL, size, prot, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
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

  void put_bytes(const char *data, size_t size) {
    memcpy(put_alloc_space(size), data, size);
  }

  void put_uint32(uint32_t val) {
    *(uint32_t *) put_alloc_space(sizeof(val)) = val;
  }

};

class CodeBuf : public DataBuffer {
public:
  CodeBuf(llvm::DataLayout *data_layout_arg, CodeGenOptions *options_arg):
      DataBuffer(PROT_READ | PROT_WRITE | PROT_EXEC),
      data_segment(PROT_READ | PROT_WRITE),
      data_layout(data_layout_arg),
      options(options_arg) {
  }

  void put_code(const char *data, size_t size) {
    put_bytes(data, size);
  }

  void put_byte(uint8_t val) {
    *(uint8_t *) put_alloc_space(sizeof(val)) = val;
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

  void put_log_message(const char *msg) {
    // pushl $desc
    put_byte(0x68);
    put_uint32((uint32_t) strdup(msg));
    put_direct_call((uintptr_t) runtime_log);
    // addl $4, %esp
    put_byte(0x81);
    put_byte(0xc4);
    put_uint32(4);
  }

  // TODO: Remove all uses of unhandled_case()!
  void unhandled_case(const char *desc) {
    fprintf(stderr, "Warning: not handled: %s\n", desc);
    // pushl $desc
    put_byte(0x68);
    put_uint32((uint32_t) strdup(desc));
    put_direct_call((uintptr_t) runtime_unhandled);
  }

  void check_offset_in_value(llvm::Type *ty, int offset) {
    if (is_i64(ty)) {
      assert(offset == 0 || offset == 4);
    } else {
      assert(offset == 0);
    }
  }

  // Generate code to put the 32-bit portion of |value| at
  // |offset_in_value| into |reg|.
  void move_part_to_reg(int reg, llvm::Value *value, int offset_in_value) {
    check_offset_in_value(value->getType(), offset_in_value);
    while (llvm::Value *alias = get_aliased_value(value))
      value = alias;
    if (llvm::Constant *cval = llvm::dyn_cast<llvm::Constant>(value)) {
      llvm::GlobalValue *global;
      uint64_t offset;
      const char *unhandled = NULL;
      expand_constant(cval, data_layout, &global, &offset, &unhandled);
      if (unhandled) {
        unhandled_case(unhandled);
        return;
      }
      if (offset_in_value == 4) {
        assert(!global); // Sanity check: globals are not 64-bit.
        offset >>= 32;
      }
      // movl $INT32, %reg
      put_byte(0xb8 | reg);
      if (global) {
        put_global_reloc(global, offset);
      } else {
        put_uint32(offset);
      }
    } else if (llvm::isa<llvm::Instruction>(value) ||
               llvm::isa<llvm::Argument>(value)) {
      assert(stackslots.count(value) == 1);
      int ebp_offset = stackslots[value] + offset_in_value;
      // movl ebp_offset(%ebp), %reg
      put_byte(0x8b);
      put_byte(0x85 | (reg << 3));
      put_uint32(ebp_offset);
    } else {
      assert(!"Unknown value type");
    }
  }

  // Generate code to put |value| into |reg|.
  void move_to_reg(int reg, llvm::Value *value) {
    assert(!is_i64(value->getType()));
    move_part_to_reg(reg, value, 0);
  }

  // Generate code to put the address of |value| into |reg|.
  void addr_to_reg(int reg, llvm::Value *value) {
    while (llvm::Value *alias = get_aliased_value(value))
      value = alias;
    if (llvm::Constant *cval = llvm::dyn_cast<llvm::Constant>(value)) {
      llvm::GlobalValue *global;
      uint64_t offset;
      const char *unhandled = NULL;
      expand_constant(cval, data_layout, &global, &offset, &unhandled);
      assert(!unhandled);
      // Put constant in data segment.
      // TODO: We could intern these constants, or avoid taking their
      // address to start with.
      assert(!global);
      char *addr = data_segment.get_current_pos();
      data_segment.put_bytes((char *) &offset, sizeof(offset));
      // movl $INT32, %reg
      put_byte(0xb8 | reg);
      put_uint32((uint32_t) addr);
    } else if (llvm::isa<llvm::Instruction>(value) ||
               llvm::isa<llvm::Argument>(value)) {
      assert(stackslots.count(value) == 1);
      int ebp_offset = stackslots[value];
      // leal ebp_offset(%ebp), %reg
      put_byte(0x8d);
      put_byte(0x85 | (reg << 3));
      put_uint32(ebp_offset);
    } else {
      assert(!"Unknown value type");
    }
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

  // Generate code to write |reg| to the 32-bit portion of the stack
  // slot for |inst| at |offset_in_value|.  This is the reverse of
  // move_part_to_reg().
  void spill_part(int reg, llvm::Instruction *inst, int offset_in_value) {
    check_offset_in_value(inst->getType(), offset_in_value);
    write_reg_to_ebp_offset(reg, stackslots[inst] + offset_in_value);
  }

  // Generate code to write |reg| to the stack slot for |inst|.  This
  // is the reverse of move_to_reg().
  void spill(int reg, llvm::Instruction *inst) {
    assert(!is_i64(inst->getType()));
    spill_part(reg, inst, 0);
  }

  void put_direct_call(uintptr_t func_addr) {
    // Direct 32-bit call.
    put_byte(0xe8);
    put_uint32(func_addr - ((uintptr_t) get_current_pos() + sizeof(uint32_t)));
  }

  void put_ret() {
    put_byte(0xc3);
  }

  void put_sized_opcode(llvm::Type *type, int opcode_base) {
    int bits;
    // TODO: Remove the use of pointer types via a rewrite pass instead.
    if (llvm::isa<llvm::PointerType>(type)) {
      bits = kPointerSizeBits;
    } else {
      llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(type);
      bits = inttype->getBitWidth();
    }
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
    labels[bb] = (uint32_t) get_current_pos();
  }

  void direct_jump_offset32(llvm::BasicBlock *dest) {
    put_uint32(0); // Placeholder
    jump_relocs.push_back(JumpReloc((uint32_t *) get_current_pos(), dest));
  }

  void put_global_reloc(llvm::GlobalValue *dest, int offset) {
    global_relocs.push_back(GlobalReloc((uint32_t *) get_current_pos(), dest));
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

  DataBuffer data_segment;

  // XXX: move somewhere better
  std::map<llvm::Value*,int> stackslots;
  std::map<llvm::BasicBlock*,uint32_t> labels;
  std::map<llvm::GlobalValue*,uint32_t> globals;
  int frame_vars_size;
  int frame_callees_args_size;

  llvm::DataLayout *data_layout;
  CodeGenOptions *options;

  typedef std::pair<uint32_t*,llvm::BasicBlock*> JumpReloc;
  std::vector<JumpReloc> jump_relocs;

  typedef std::pair<uint32_t*,llvm::GlobalValue*> GlobalReloc;
  std::vector<GlobalReloc> global_relocs;
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
    llvm::Value *incoming = phi->getIncomingValueForBlock(from_bb);
    if (is_i64(phi->getType())) {
      codebuf.move_part_to_reg(tmp_reg, incoming, 0);
      codebuf.spill_part(tmp_reg, phi, 0);
      codebuf.move_part_to_reg(tmp_reg, incoming, 4);
      codebuf.spill_part(tmp_reg, phi, 4);
    } else {
      codebuf.move_to_reg(tmp_reg, incoming);
      codebuf.spill(tmp_reg, phi);
    }
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

int get_arg_stack_size(llvm::Type *arg_type) {
  return is_i64(arg_type) ? 8 : 4;
}

int get_args_stack_size(llvm::CallInst *call) {
  int size = 0;
  for (unsigned i = 0; i < call->getNumArgOperands(); ++i) {
    size += get_arg_stack_size(call->getArgOperand(i)->getType());
  }
  return size;
}

const char *get_instruction_type(llvm::Instruction *inst) {
  switch (inst->getOpcode()) {
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    case llvm::Instruction::OPCODE: return #OPCODE;
#include <llvm/IR/Instruction.def>
#undef HANDLE_INST
    default: return "<unknown-instruction>";
  }
}

void translate_instruction(llvm::Instruction *inst, CodeBuf &codebuf) {
  if (llvm::BinaryOperator *op =
      llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
    if (op->getType()->isDoubleTy()) {
      codebuf.unhandled_case("FP arithmetic");
      return;
    }
    llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(op->getType());
    int bits = inttype->getBitWidth();
    if (bits < 8) {
      assert(bits == 1);
      switch (op->getOpcode()) {
        case llvm::Instruction::And:
        case llvm::Instruction::Or:
        case llvm::Instruction::Xor:
          break;
        default:
          // Non-logic operations are awkward to test and are of
          // dubious usefulness.  e.g. Shifting a 1-bit value by 1
          // gives undefined behaviour.
          assert(!"Only logic operations are supported on i1");
      }
    }
    if (bits == 64) {
      // Generate function call to helper function.
      int arg_size = 4;
      int args_size = arg_size * 3;
      assert(kMinCalleeArgsSize >= args_size);
      assert(codebuf.frame_callees_args_size >= args_size);
      // Argument 1: result pointer
      codebuf.addr_to_reg(REG_EAX, inst);
      codebuf.write_reg_to_esp_offset(REG_EAX, arg_size * 0);
      // Argument 2: pointer to operand
      codebuf.addr_to_reg(REG_EAX, inst->getOperand(0));
      codebuf.write_reg_to_esp_offset(REG_EAX, arg_size * 1);
      // Argument 3: pointer to operand
      codebuf.addr_to_reg(REG_EAX, inst->getOperand(1));
      codebuf.write_reg_to_esp_offset(REG_EAX, arg_size * 2);

      uintptr_t func;
      switch (op->getOpcode()) {
#define MAP(OP) \
    case llvm::Instruction::OP: \
      func = (uintptr_t) runtime_i64_##OP; break
        MAP(Add);
        MAP(Sub);
        MAP(Mul);
        MAP(UDiv);
        MAP(URem);
        MAP(SDiv);
        MAP(SRem);
        MAP(And);
        MAP(Or);
        MAP(Xor);
        MAP(Shl);
        MAP(LShr);
        MAP(AShr);
#undef MAP
        default:
          assert(!"Unknown binary operator");
      }
      codebuf.put_direct_call(func);
      return;
    }

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
        codebuf.extend_to_i32(REG_EAX, true, bits);
        codebuf.extend_to_i32(REG_ECX, true, bits);
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
        codebuf.extend_to_i32(REG_EAX, false, bits);
        codebuf.put_code(TEMPL("\xd3\xe8")); // shr %cl, %eax
        codebuf.spill(REG_EAX, inst);
        break;
      }
      case llvm::Instruction::AShr: {
        codebuf.extend_to_i32(REG_EAX, true, bits);
        codebuf.put_code(TEMPL("\xd3\xf8")); // sar %cl, %eax
        codebuf.spill(REG_EAX, inst);
        break;
      }
      default:
        assert(!"Unknown binary operator");
    }
  } else if (llvm::ICmpInst *op = llvm::dyn_cast<llvm::ICmpInst>(inst)) {
    llvm::Type *operand_type = op->getOperand(0)->getType();
    int bits;
    // TODO: Remove the use of pointer types via a rewrite pass instead.
    if (llvm::isa<llvm::PointerType>(operand_type)) {
      bits = kPointerSizeBits;
    } else {
      llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(operand_type);
      bits = inttype->getBitWidth();
    }
    assert(bits >= 8); // Disallow i1
    if (bits == 64) {
      // Generate function call to helper function.
      int arg_size = 4;
      int args_size = arg_size * 2;
      assert(kMinCalleeArgsSize >= args_size);
      assert(codebuf.frame_callees_args_size >= args_size);
      // Argument 1: pointer to operand
      codebuf.addr_to_reg(REG_EAX, inst->getOperand(0));
      codebuf.write_reg_to_esp_offset(REG_EAX, arg_size * 0);
      // Argument 2: pointer to operand
      codebuf.addr_to_reg(REG_EAX, inst->getOperand(1));
      codebuf.write_reg_to_esp_offset(REG_EAX, arg_size * 1);

      uintptr_t func;
      switch (op->getPredicate()) {
#define MAP(OP) \
    case llvm::CmpInst::OP: \
      func = (uintptr_t) runtime_i64_##OP; break
        MAP(ICMP_EQ);
        MAP(ICMP_NE);
        MAP(ICMP_UGT);
        MAP(ICMP_UGE);
        MAP(ICMP_ULT);
        MAP(ICMP_ULE);
        MAP(ICMP_SGT);
        MAP(ICMP_SGE);
        MAP(ICMP_SLT);
        MAP(ICMP_SLE);
#undef MAP
        default:
          assert(!"Unknown binary operator");
      }
      codebuf.put_direct_call(func);
      codebuf.spill(REG_EAX, inst);
      return;
    }

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
    if (op->getType()->isDoubleTy()) {
      codebuf.unhandled_case("FP memory load");
      return;
    }
    codebuf.move_to_reg(REG_EAX, op->getPointerOperand());
    if (is_i64(op->getType())) {
      codebuf.addr_to_reg(REG_EDX, op);
      codebuf.put_code(TEMPL("\x8b\x08")); // movl (%eax), %ecx
      codebuf.put_code(TEMPL("\x89\x0a")); // movl %ecx, (%edx)
      codebuf.put_code(TEMPL("\x8b\x48\x04")); // movl 4(%eax), %ecx
      codebuf.put_code(TEMPL("\x89\x4a\x04")); // movl %ecx, 4(%edx)
    } else {
      // mov<size> (%eax), %eax
      codebuf.put_sized_opcode(op->getType(), 0x8a);
      codebuf.put_byte(0x00);
      codebuf.spill(REG_EAX, inst);
    }
  } else if (llvm::StoreInst *op = llvm::dyn_cast<llvm::StoreInst>(inst)) {
    if (op->getType()->isDoubleTy()) {
      codebuf.unhandled_case("FP memory store");
      return;
    }
    codebuf.move_to_reg(REG_EDX, op->getPointerOperand());
    if (is_i64(op->getValueOperand()->getType())) {
      codebuf.addr_to_reg(REG_EAX, op->getValueOperand());
      codebuf.put_code(TEMPL("\x8b\x08")); // movl (%eax), %ecx
      codebuf.put_code(TEMPL("\x89\x0a")); // movl %ecx, (%edx)
      codebuf.put_code(TEMPL("\x8b\x48\x04")); // movl 4(%eax), %ecx
      codebuf.put_code(TEMPL("\x89\x4a\x04")); // movl %ecx, 4(%edx)
    } else {
      codebuf.move_to_reg(REG_EAX, op->getValueOperand());
      // mov<size> %eax, (%edx)
      codebuf.put_sized_opcode(op->getValueOperand()->getType(), 0x88);
      codebuf.put_byte(0x02);
    }
  } else if (llvm::AtomicRMWInst *op =
             llvm::dyn_cast<llvm::AtomicRMWInst>(inst)) {
    llvm::IntegerType *inttype = llvm::cast<llvm::IntegerType>(op->getType());
    if (inttype->getBitWidth() != 32) {
      codebuf.unhandled_case("AtomicRMWInst on non-i32");
      return;
    }
    // XXX: We assume seq_cst (SequentiallyConsistent).
    // We also assume a SynchronizationScope of CrossThread.
    uintptr_t func;
    switch (op->getOperation()) {
#define MAP(OP) case llvm::AtomicRMWInst::OP: \
                  func = (uintptr_t) runtime_atomicrmw_i32_##OP; break;
      MAP(Xchg)
      MAP(Add)
      MAP(Sub)
      MAP(And)
      MAP(Nand)
      MAP(Or)
      MAP(Xor)
      MAP(Max)
      MAP(Min)
      MAP(UMax)
      MAP(UMin)
#undef MAP
      default:
        assert(!"Unknown comparison");
    }
    // Generate function call to helper function.
    codebuf.move_to_reg(REG_EAX, op->getPointerOperand());
    codebuf.move_to_reg(REG_EDX, op->getValOperand());
    codebuf.put_direct_call(func);
    codebuf.spill(REG_EAX, inst);
  } else if (llvm::ReturnInst *op
             = llvm::dyn_cast<llvm::ReturnInst>(inst)) {
    if (llvm::Value *result = op->getReturnValue()) {
      if (result->getType()->isDoubleTy()) {
        codebuf.addr_to_reg(REG_EAX, result);
        // fldl (%eax)
        codebuf.put_code(TEMPL("\xdd\x00"));
      } else if (is_i64(result->getType())) {
        codebuf.move_part_to_reg(REG_EAX, result, 0);
        codebuf.move_part_to_reg(REG_EDX, result, 4);
      } else {
        codebuf.move_to_reg(REG_EAX, result);
      }
    }
    // Epilog:
    codebuf.put_byte(0xc9); // leave
    // Omit-frame-pointer version:
    // // addl $frame_size, %esp
    // codebuf.put_byte(0x81);
    // codebuf.put_byte(0xc4);
    // codebuf.put_uint32(codebuf.frame_size);
    codebuf.put_ret();
  } else if (llvm::SelectInst *op = llvm::dyn_cast<llvm::SelectInst>(inst)) {
    // We could use the CMOV instruction here, but it's not available
    // on old x86-32 CPUs.
    codebuf.move_to_reg(REG_EAX, op->getCondition());
    codebuf.move_to_reg(REG_ECX, op->getTrueValue());
    codebuf.put_code(TEMPL("\x84\xc0")); // testb %eax, %eax

    // TODO: Could use 8-bit jump.
    codebuf.put_code(TEMPL("\x0f\x85")); // jnz <label> (32-bit)
    uint32_t *jump_dest =
      (uint32_t *) codebuf.put_alloc_space(sizeof(uint32_t));

    codebuf.move_to_reg(REG_ECX, op->getFalseValue());
    // Fix up relocation.
    uintptr_t label = (uintptr_t) codebuf.get_current_pos();
    *jump_dest = label - (uintptr_t) (jump_dest + 1);
    codebuf.spill(REG_ECX, op);
  } else if (llvm::BranchInst *op =
             llvm::dyn_cast<llvm::BranchInst>(inst)) {
    // TODO: could implement fallthrough to next basic block
    llvm::BasicBlock *bb = inst->getParent();
    if (op->isConditional()) {
      handle_phi_nodes(bb, op->getSuccessor(0), codebuf, REG_EAX);
      codebuf.move_to_reg(REG_EAX, op->getCondition());
      // We must test only the bottom bit of %eax, since the other
      // bits can contain garbage.
      codebuf.put_code(TEMPL("\xa8\x01")); // testb $1, %al
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
    assert(bits >= 8); // Disallow i1

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
    if (is_i64(inst->getType())) {
      // Same as spill(REG_EAX, inst), without the i64 check.
      int stack_offset = codebuf.stackslots[inst];
      codebuf.write_reg_to_ebp_offset(REG_EAX, stack_offset);
      if (sign_extend) {
        // Fill %edx with sign bit of %eax
        codebuf.put_code(TEMPL("\x99")); // cltd (cdq in Intel syntax)
        codebuf.write_reg_to_ebp_offset(REG_EDX, stack_offset + 4);
      } else {
        // movl $0, offset(%ebp)
        codebuf.put_code(TEMPL("\xc7\x85"));
        codebuf.put_uint32(stack_offset + 4); // Displacement
        codebuf.put_uint32(0); // Immediate
      }
    } else {
      codebuf.spill(REG_EAX, inst);
    }
  } else if (llvm::IntrinsicInst *op =
             llvm::dyn_cast<llvm::IntrinsicInst>(inst)) {
    llvm::Intrinsic::ID id = op->getIntrinsicID();
    // TODO: llvm.dbg.value and llvm.dbg.declare should be stripped
    // out in the wire format, and we should disallow use of these.
    if (id == llvm::Intrinsic::lifetime_start ||
        id == llvm::Intrinsic::lifetime_end ||
        id == llvm::Intrinsic::dbg_value ||
        id == llvm::Intrinsic::dbg_declare) {
      // Ignore.
    } else {
      std::string desc = "IntrinsicInst: ";
      desc += op->getCalledValue()->getName();
      codebuf.unhandled_case(desc.c_str());
    }
  } else if (llvm::CallInst *op = llvm::dyn_cast<llvm::CallInst>(inst)) {
    // We have already reserved space on the stack to store our
    // callee's argument.
    int stack_offset = 0;
    for (unsigned i = 0; i < op->getNumArgOperands(); ++i) {
      llvm::Value *arg = op->getArgOperand(i);
      if (is_i64(arg->getType())) {
        codebuf.addr_to_reg(REG_EAX, arg);
        codebuf.put_code(TEMPL("\x8b\x10")); // movl (%eax), %edx
        codebuf.write_reg_to_esp_offset(REG_EDX, stack_offset);
        codebuf.put_code(TEMPL("\x8b\x50\x04")); // movl 4(%eax), %edx
        codebuf.write_reg_to_esp_offset(REG_EDX, stack_offset + 4);
        stack_offset += 8;
      } else {
        codebuf.move_to_reg(REG_EAX, arg);
        codebuf.write_reg_to_esp_offset(REG_EAX, stack_offset);
        stack_offset += 4;
      }
    }
    // TODO: Optimise to output direct calls too
    codebuf.move_to_reg(REG_EAX, op->getCalledValue());
    codebuf.put_code(TEMPL("\xff\xd0")); // call *%eax
    if (is_i64(op->getType())) {
      codebuf.spill_part(REG_EAX, op, 0);
      codebuf.spill_part(REG_EDX, op, 4);
    } else {
      codebuf.spill(REG_EAX, op);
    }
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
  } else if (llvm::isa<llvm::UnreachableInst>(inst)) {
    // We don't have to output anything here, but it's better to make
    // the program fail fast than do something undefined by running
    // into the next basic block.
    codebuf.put_byte(0xf4); // hlt
  } else if (codebuf.get_aliased_value(inst)) {
    // Nothing to do:  handled elsewhere.
  } else {
    codebuf.unhandled_case(get_instruction_type(inst));
  }
}

void translate_bb(llvm::BasicBlock *bb, CodeBuf &codebuf) {
  codebuf.make_label(bb);
  if (codebuf.options->trace_logging)
    codebuf.put_log_message((std::string("  block: ") +
                             std::string(bb->getName())).c_str());
  for (llvm::BasicBlock::InstListType::iterator inst = bb->begin();
       inst != bb->end();
       ++inst) {
    translate_instruction(inst, codebuf);
  }
}

void write_global(CodeBuf *codebuf, llvm::Constant *init) {
  DataBuffer *dataseg = &codebuf->data_segment;

  if (llvm::isa<llvm::ConstantAggregateZero>(init) ||
      llvm::isa<llvm::UndefValue>(init)) {
    int size = codebuf->data_layout->getTypeAllocSize(init->getType());
    dataseg->put_alloc_space(size);
  } else if (llvm::ConstantArray *val =
             llvm::dyn_cast<llvm::ConstantArray>(init)) {
    for (unsigned i = 0; i < val->getNumOperands(); ++i) {
      write_global(codebuf, val->getOperand(i));
    }
  } else if (llvm::ConstantDataSequential *val =
             llvm::dyn_cast<llvm::ConstantDataSequential>(init)) {
    // Note that getRawDataValues() assumes the host endianness is the same.
    llvm::StringRef str = val->getRawDataValues();
    dataseg->put_bytes(str.data(), str.size());
  } else if (llvm::ConstantStruct *val =
             llvm::dyn_cast<llvm::ConstantStruct>(init)) {
    const llvm::StructLayout *layout = codebuf->data_layout->getStructLayout(
        val->getType());
    uint64_t prev_offset = 0;
    for (unsigned i = 0; i < val->getNumOperands(); ++i) {
      llvm::Constant *field = val->getOperand(i);
      write_global(codebuf, field);

      // Add padding.
      uint64_t next_offset =
        i == val->getNumOperands() - 1
        ? codebuf->data_layout->getTypeAllocSize(init->getType())
        : layout->getElementOffset(i + 1);
      uint64_t field_size =
        codebuf->data_layout->getTypeAllocSize(field->getType());
      uint64_t padding_size = next_offset - prev_offset - field_size;
      dataseg->put_alloc_space(padding_size);
      prev_offset = next_offset;
    }
  } else {
    // TODO: unify fully with expand_constant().
    llvm::GlobalValue *global;
    uint64_t offset;
    const char *unhandled = NULL;
    expand_constant(init, codebuf->data_layout, &global, &offset, &unhandled);
    assert(!unhandled);

    uint32_t size = codebuf->data_layout->getTypeAllocSize(init->getType());
    if (global) {
      assert(size == sizeof(uint32_t));
      assert((uint32_t) offset == offset);
      // This mirrors put_global_reloc().
      // TODO: unify these.
      codebuf->global_relocs.push_back(
          CodeBuf::GlobalReloc((uint32_t *) dataseg->get_current_pos(),
                               global));
      dataseg->put_uint32(offset);
    } else {
      // Assumes little endian.
      dataseg->put_bytes((char *) &offset, size);
    }
  }
}

// Expand memcpy intrinsic to a call to the host's memcpy() function.
// Same for memset().
// TODO: Expand this in the original bitcode file.
void expand_mem_intrinsics(llvm::BasicBlock *bb) {
  for (llvm::BasicBlock::InstListType::iterator iter = bb->begin();
       iter != bb->end(); ) {
    llvm::Instruction *inst = iter++;
    if (llvm::MemIntrinsic *op = llvm::dyn_cast<llvm::MemIntrinsic>(inst)) {
      llvm::Module *module = bb->getParent()->getParent();
      llvm::Type *i8 = llvm::Type::getInt8Ty(module->getContext());
      // Note that we ignore op->getDestAddressSpace() and
      // op->getSourceAddressSpace(): we only support one address
      // space.
      llvm::Type *i8ptr = i8->getPointerTo();
      llvm::Type *sizetype = llvm::Type::getInt32Ty(module->getContext());
      std::vector<llvm::Type*> arg_types;
      std::vector<llvm::Value*> args;
      uintptr_t mem_func;
      if (llvm::MemTransferInst *op =
          llvm::dyn_cast<llvm::MemTransferInst>(inst)) {
        arg_types.push_back(i8ptr);
        arg_types.push_back(i8ptr);
        arg_types.push_back(sizetype);
        args.push_back(op->getRawDest());
        args.push_back(op->getRawSource());
        if (llvm::isa<llvm::MemCpyInst>(inst)) {
          mem_func = (uintptr_t) memcpy;
        } else if (llvm::isa<llvm::MemMoveInst>(inst)) {
          mem_func = (uintptr_t) memmove;
        } else {
          assert(!"Unknown MemTransferInst");
        }
      } else if (llvm::MemSetInst *op =
                 llvm::dyn_cast<llvm::MemSetInst>(inst)) {
        arg_types.push_back(i8ptr);
        arg_types.push_back(i8);
        arg_types.push_back(sizetype);
        args.push_back(op->getRawDest());
        args.push_back(op->getValue());
        mem_func = (uintptr_t) memset;
      } else {
        assert(!"Unknown memory intrinsic");
      }
      // The intrinsics come in variants with i32 and i64 lengths.  We
      // truncate the i64 down to i32.  We do no checks to see whether
      // this discards bits!
      llvm::Value *length = op->getLength();
      if (length->getType() != sizetype)
        length = new llvm::TruncInst(op->getLength(), sizetype,
                                     "length_cast", op);
      args.push_back(length);
      llvm::Type *mem_func_type =
        llvm::PointerType::get(
            llvm::FunctionType::get(llvm::Type::getVoidTy(module->getContext()),
                                    arg_types, false), 0);
      llvm::Value *func = llvm::ConstantExpr::getIntToPtr(
          llvm::ConstantInt::get(sizetype, mem_func), mem_func_type);

      // TODO: Support volatile operations.  The standard memcpy() is
      // non-volatile.
      assert(!op->isVolatile());
      llvm::Value *new_call =
        llvm::CallInst::Create(func, args, op->getName(), op);
      op->replaceAllUsesWith(new_call);
      op->eraseFromParent();
    }
  }
}

void translate_function(llvm::Function *func, CodeBuf &codebuf) {
  llvm::FunctionPass *expand_constantexpr = createExpandConstantExprPass();
  llvm::BasicBlockPass *expand_gep = createExpandGetElementPtrPass();

  int callees_args_size = kMinCalleeArgsSize;
  expand_constantexpr->runOnFunction(*func);
  for (llvm::Function::iterator bb = func->begin();
       bb != func->end();
       ++bb) {
    expand_gep->runOnBasicBlock(*bb);
    expand_mem_intrinsics(bb);
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

  int arg_offset = 8; // Skip return address and frame pointer
  for (llvm::Function::ArgumentListType::iterator arg = func->arg_begin();
       arg != func->arg_end();
       ++arg) {
    assert(codebuf.stackslots.count(arg) == 0);
    codebuf.stackslots[arg] = arg_offset;
    arg_offset += get_arg_stack_size(arg->getType());
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
        vars_size += get_arg_stack_size(inst->getType());
        codebuf.stackslots[inst] = -vars_size;
      }
    }
  }
  codebuf.frame_vars_size = vars_size;
  int frame_size = codebuf.frame_vars_size + codebuf.frame_callees_args_size;

  char *function_entry = codebuf.get_current_pos();
  if (func->empty()) {
    if (func->getName() == "llvm.nacl.read.tp") {
      function_entry = (char *) runtime_tls_get;
    } else {
      std::string msg = "Function declared but not defined: ";
      msg += func->getName();
      codebuf.unhandled_case(msg.c_str());
    }
  } else {
    // Prolog:
    codebuf.put_byte(0x55); // pushl %ebp
    codebuf.put_code(TEMPL("\x89\xe5")); // movl %esp, %ebp
    // subl $frame_size, %esp
    codebuf.put_byte(0x81);
    codebuf.put_byte(0xec);
    codebuf.put_uint32(frame_size);

    if (codebuf.options->trace_logging)
      codebuf.put_log_message((std::string("func: ") +
                               std::string(func->getName())).c_str());

    for (llvm::Function::iterator bb = func->begin();
         bb != func->end();
         ++bb) {
      translate_bb(bb, codebuf);
    }
  }

  if (codebuf.options->dump_code) {
    printf("%s:\n", func->getName().str().c_str());
    fflush(stdout);
    dump_range_as_code(function_entry, codebuf.get_current_pos());
  }

  codebuf.globals[func] = (uintptr_t) function_entry;

  delete expand_constantexpr;
  delete expand_gep;
}

void translate(llvm::Module *module, std::map<std::string,uintptr_t> *globals,
               CodeGenOptions *options) {
  llvm::DataLayout data_layout(module);
  CodeBuf codebuf(&data_layout, options);

  llvm::ModulePass *expand_varargs = createExpandVarArgsPass();
  expand_varargs->runOnModule(*module);
  delete expand_varargs;

  for (llvm::Module::GlobalListType::iterator global = module->global_begin();
       global != module->global_end();
       ++global) {
    assert(!global->isThreadLocal());
    if (global->hasInitializer()) {
      // TODO: handle alignments
      uint32_t addr = (uint32_t) codebuf.data_segment.get_current_pos();
      size_t size =
        data_layout.getTypeAllocSize(global->getType()->getElementType());
      codebuf.globals[global] = (uint32_t) addr;
      write_global(&codebuf, global->getInitializer());
      assert(codebuf.data_segment.get_current_pos() == (char *) addr + size);
    } else {
      // TODO: Disallow this case.
      assert(global->getLinkage() == llvm::GlobalValue::ExternalWeakLinkage);
      std::string name = global->getName().str();
      if (name != "__ehdr_start" &&
          name != "__preinit_array_start" &&
          name != "__preinit_array_end") {
        fprintf(stderr, "Disallowed extern_weak symbol: %s\n",
                global->getName().str().c_str());
        assert(0);
      }
      codebuf.globals[global] = 0;
    }
  }

  for (llvm::Module::FunctionListType::iterator func = module->begin();
       func != module->end();
       ++func) {
    translate_function(func, codebuf);
  }
  codebuf.apply_jump_relocs();
  codebuf.apply_global_relocs();

  llvm::verifyModule(*module);

  for (std::map<llvm::GlobalValue*,uint32_t>::iterator global =
         codebuf.globals.begin();
       global != codebuf.globals.end();
       ++global) {
    (*globals)[global->first->getName()] = global->second;
  }
}
