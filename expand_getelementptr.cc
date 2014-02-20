//===- expand_getelementptr.hcc- A pass for simplifying LLVM IR------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "expand_getelementptr.h"

#include <llvm/BasicBlock.h>
#include <llvm/Constants.h>
#include <llvm/Function.h>
#include <llvm/InstrTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>

// In LLVM 3.2, this becomes <llvm/DataLayout.h>
#include <llvm/Target/TargetData.h>

using namespace llvm;

namespace {
  class ExpandGetElementPtr : public BasicBlockPass {
  public:
    static char ID; // Pass identification, replacement for typeid
    ExpandGetElementPtr() : BasicBlockPass(ID) {
    }

    virtual bool runOnBasicBlock(BasicBlock &bb);
  };
}

char ExpandGetElementPtr::ID = 0;

bool ExpandGetElementPtr::runOnBasicBlock(BasicBlock &bb) {
  bool modified = false;
  Module *module = bb.getParent()->getParent();
  Type *ptrtype = Type::getInt32Ty(module->getContext());
  TargetData data_layout(module);

  for (BasicBlock::InstListType::iterator iter = bb.begin();
       iter != bb.end(); ) {
    Instruction *inst = iter++;
    if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(inst)) {
      modified = true;

      // TODO: If the operand is an IntToPtrInst, we could reuse its
      // operand rather than converting back (assuming it's the right
      // size).
      Value *ptr = new PtrToIntInst(gep->getPointerOperand(), ptrtype,
                                    "gep", gep);

      Type *ty = gep->getPointerOperand()->getType();
      for (GetElementPtrInst::op_iterator op = inst->op_begin() + 1;
           op != inst->op_end();
           ++op) {
        Value *index = *op;
        Value *offset_val;
        if (StructType *stty = dyn_cast<StructType>(ty)) {
          uint64_t field = cast<ConstantInt>(op)->getZExtValue();
          uint64_t offset =
            data_layout.getStructLayout(stty)->getElementOffset(field);
          offset_val = ConstantInt::get(ptrtype, offset);
          ty = stty->getElementType(field);
        } else {
          ty = cast<SequentialType>(ty)->getElementType();
          uint64_t element_size = data_layout.getTypeAllocSize(ty);
          offset_val = BinaryOperator::Create(
              Instruction::Mul, index,
              ConstantInt::get(ptrtype, element_size),
              "gep_array", gep);
        }
        // TODO: Omit if offset is zero, or combine additions.
        ptr = BinaryOperator::Create(Instruction::Add, ptr, offset_val,
                                     "gep", gep);
      }

      assert(ty == gep->getType()->getElementType());
      Value *result = new IntToPtrInst(ptr, gep->getType(), "gep", gep);
      gep->replaceAllUsesWith(result);
      gep->eraseFromParent();
    }
  }
  return modified;
}

BasicBlockPass *createExpandGetElementPtrPass() {
  return new ExpandGetElementPtr();
}
