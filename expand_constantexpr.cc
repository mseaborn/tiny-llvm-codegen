
#include "expand_constantexpr.h"

#include <llvm/BasicBlock.h>
#include <llvm/Constants.h>
#include <llvm/Function.h>
#include <llvm/InstrTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Module.h>
#include <llvm/Operator.h>
#include <llvm/Pass.h>

using namespace llvm;

Value *expandConstantExpr(Value *Val, Instruction *InsertPt);

namespace {
  class ExpandConstantExpr : public BasicBlockPass {
  public:
    static char ID; // Pass identification, replacement for typeid
    ExpandConstantExpr() : BasicBlockPass(ID) {
    }

    virtual bool runOnBasicBlock(BasicBlock &bb);
  };
}

char ExpandConstantExpr::ID = 0;

// Based on ConstantExpr::getAsInstruction() in lib/VMCore/Constants.cpp.
// TODO: Use getAsInstruction() when we require a newer LLVM version.
Instruction *getConstantExprAsInstruction(ConstantExpr *CE,
                                          Instruction *InsertPt) {
  SmallVector<Value*,4> ValueOperands;
  for (ConstantExpr::op_iterator I = CE->op_begin(), E = CE->op_end();
       I != E;
       ++I)
    ValueOperands.push_back(expandConstantExpr(cast<Value>(I), InsertPt));

  ArrayRef<Value*> Ops(ValueOperands);

  switch (CE->getOpcode()) {
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
    return CastInst::Create((Instruction::CastOps)CE->getOpcode(),
                            Ops[0], CE->getType());
  case Instruction::Select:
    return SelectInst::Create(Ops[0], Ops[1], Ops[2]);
  case Instruction::InsertElement:
    return InsertElementInst::Create(Ops[0], Ops[1], Ops[2]);
  case Instruction::ExtractElement:
    return ExtractElementInst::Create(Ops[0], Ops[1]);
  case Instruction::InsertValue:
    return InsertValueInst::Create(Ops[0], Ops[1], CE->getIndices());
  case Instruction::ExtractValue:
    return ExtractValueInst::Create(Ops[0], CE->getIndices());
  case Instruction::ShuffleVector:
    return new ShuffleVectorInst(Ops[0], Ops[1], Ops[2]);

  case Instruction::GetElementPtr:
    if (cast<GEPOperator>(CE)->isInBounds())
      return GetElementPtrInst::CreateInBounds(Ops[0], Ops.slice(1));
    else
      return GetElementPtrInst::Create(Ops[0], Ops.slice(1));

  case Instruction::ICmp:
  case Instruction::FCmp:
    return CmpInst::Create((Instruction::OtherOps)CE->getOpcode(),
                           CE->getPredicate(), Ops[0], Ops[1]);

  default:
    assert(CE->getNumOperands() == 2 && "Must be binary operator?");
    BinaryOperator *BO =
      BinaryOperator::Create((Instruction::BinaryOps)CE->getOpcode(),
                             Ops[0], Ops[1]);
    if (OverflowingBinaryOperator *Op =
        dyn_cast<OverflowingBinaryOperator>(BO)) {
      BO->setHasNoUnsignedWrap(Op->hasNoUnsignedWrap());
      BO->setHasNoSignedWrap(Op->hasNoSignedWrap());
    }
    if (PossiblyExactOperator *Op = dyn_cast<PossiblyExactOperator>(BO))
      BO->setIsExact(Op->isExact());
    return BO;
  }
}

Value *expandConstantExpr(Value *Val, Instruction *InsertPt) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Val)) {
    Instruction *NewInst = getConstantExprAsInstruction(CE, InsertPt);
    NewInst->insertBefore(InsertPt);
    NewInst->setName("expanded");
    return NewInst;
  }
  return Val;
}

bool ExpandConstantExpr::runOnBasicBlock(BasicBlock &bb) {
  bool modified = false;
  for (BasicBlock::InstListType::iterator inst = bb.begin();
       inst != bb.end();
       ++inst) {
    for (unsigned opnum = 0; opnum < inst->getNumOperands(); opnum++) {
      if (ConstantExpr *expr =
          dyn_cast<ConstantExpr>(inst->getOperand(opnum))) {
        modified = true;
        Instruction *insert_pt = inst;
        if (PHINode *pn = dyn_cast<PHINode>(insert_pt)) {
          // We cannot insert instructions before a PHI node, so
          // insert before the incoming block's terminator.  This
          // could be suboptimal if the terminator is a conditional.
          insert_pt = pn->getIncomingBlock(opnum)->getTerminator();
        }
        inst->setOperand(opnum, expandConstantExpr(expr, insert_pt));
      }
    }
  }
  return modified;
}

BasicBlockPass *createExpandConstantExprPass() {
  return new ExpandConstantExpr();
}
