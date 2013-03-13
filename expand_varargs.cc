
#include "expand_varargs.h"

#include <llvm/BasicBlock.h>
#include <llvm/Constants.h>
#include <llvm/Function.h>
#include <llvm/InstrTypes.h>
#include <llvm/Instructions.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>

// In LLVM 3.2, this becomes <llvm/DataLayout.h>
#include <llvm/Target/TargetData.h>

using namespace llvm;

namespace {
  class ExpandVarArgs : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid
    ExpandVarArgs() : ModulePass(ID) {
    }

    virtual bool runOnModule(Module &M);
  };
}

char ExpandVarArgs::ID = 0;

static void ExpandVarArgFunc(Function *Func) {
  Module *Module = Func->getParent();
  Type *PtrType = Type::getInt8Ty(Module->getContext())->getPointerTo();

  FunctionType *FTy = Func->getFunctionType();
  std::vector<Type*> Params(FTy->param_begin(), FTy->param_end());
  Params.push_back(PtrType);
  FunctionType *NFTy = FunctionType::get(FTy->getReturnType(), Params, false);

  // In order to change the function's arguments, we have to recreate
  // the function.
  Function *NewFunc = Function::Create(NFTy, Func->getLinkage());
  NewFunc->copyAttributesFrom(Func);
  Func->getParent()->getFunctionList().insert(Func, NewFunc);
  NewFunc->takeName(Func);
  NewFunc->getBasicBlockList().splice(NewFunc->begin(),
                                      Func->getBasicBlockList());

  // Move the arguments across to the new function.
  for (Function::arg_iterator Arg = Func->arg_begin(), E = Func->arg_end(),
         NewArg = NewFunc->arg_begin();
       Arg != E; ++Arg, ++NewArg) {
    Arg->replaceAllUsesWith(NewArg);
    NewArg->takeName(Arg);
  }

  Func->replaceAllUsesWith(
      ConstantExpr::getBitCast(NewFunc, FTy->getPointerTo()));
  Func->eraseFromParent();

  Value *VarArgsArg = --NewFunc->arg_end();
  VarArgsArg->setName("varargs");

  for (Function::iterator BB = NewFunc->begin(), E = NewFunc->end();
       BB != E;
       ++BB) {
    for (BasicBlock::iterator Iter = BB->begin(), E = BB->end();
         Iter != E; ) {
      Instruction *Inst = Iter++;
      // TODO: Use VAStartInst when we upgrade LLVM version.
      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
        if (II->getIntrinsicID() == Intrinsic::vastart) {
          Value *Cast = new BitCastInst(II->getOperand(0),
                                        PtrType->getPointerTo(),
                                        "arglist", II);
          new StoreInst(VarArgsArg, Cast, II);
          II->eraseFromParent();
        } else if (II->getIntrinsicID() == Intrinsic::vaend) {
          II->eraseFromParent();
        }
      }
    }
  }

  // TODO: Update debug information too.
}

static void ExpandVAArgInst(VAArgInst *Inst, TargetData *DataLayout) {
  Module *Module = Inst->getParent()->getParent()->getParent();
  Type *I8 = Type::getInt8Ty(Module->getContext());
  Type *I32 = Type::getInt32Ty(Module->getContext());

  // Read the argument.
  Value *ArgList = new BitCastInst(Inst->getPointerOperand(),
                                   I8->getPointerTo()->getPointerTo(),
                                   "arglist", Inst);
  Value *CurrentPtr = new LoadInst(ArgList, "arglist_current", Inst);
  Value *Result =
    new LoadInst(new BitCastInst(CurrentPtr, Inst->getType()->getPointerTo(),
                                 "va_arg_ptr", Inst),
                 "va_arg", Inst);

  // Update the va_list to point to the next argument.
  // TODO: Add alignment.
  unsigned Offset = DataLayout->getTypeAllocSize(Inst->getType());
  std::vector<Value*> Indexes;
  Indexes.push_back(ConstantInt::get(I32, Offset));
  Value *Next = GetElementPtrInst::Create(CurrentPtr, Indexes,
                                          "arglist_next", Inst);
  new StoreInst(Next, ArgList, Inst);

  Inst->replaceAllUsesWith(Result);
  Inst->eraseFromParent();
}

static bool ExpandVarArgCall(CallInst *Call) {
  FunctionType *FuncType = cast<FunctionType>(
      Call->getCalledValue()->getType()->getPointerElementType());
  if (!FuncType->isFunctionVarArg())
    return false;
  // If there are no variable arguments being passed, nothing needs to
  // be changed.  Also, StructType::create() rejects empty lists.
  if (FuncType->getNumParams() == Call->getNumArgOperands())
    return false;

  LLVMContext *Context =
    &Call->getParent()->getParent()->getParent()->getContext();

  // Split argument list into fixed and variable arguments.
  std::vector<Value*> FixedArgs;
  std::vector<Value*> VarArgs;
  for (unsigned I = 0; I < FuncType->getNumParams(); ++I)
    FixedArgs.push_back(Call->getArgOperand(I));
  for (unsigned I = FuncType->getNumParams();
       I < Call->getNumArgOperands(); ++I)
    VarArgs.push_back(Call->getArgOperand(I));

  // Create struct type for packing variable arguments into.
  std::vector<Type*> VarArgsTypes;
  for (std::vector<Value*>::iterator Iter = VarArgs.begin();
       Iter != VarArgs.end();
       ++Iter) {
    VarArgsTypes.push_back((*Iter)->getType());
  }
  // TODO: We create this as packed for now, but we might need to add
  // alignments later.
  StructType *VarArgsTy = StructType::create(VarArgsTypes, "vararg_call", true);

  // Allocate space for the variable argument buffer.  Do this at the
  // start of the function so that we don't leak space if the function
  // is called in a loop.
  Function *Func = Call->getParent()->getParent();
  Instruction *Buf = new AllocaInst(VarArgsTy, "vararg_buffer");
  Func->getEntryBlock().getInstList().push_front(Buf);

  // TODO: Add calls to llvm.lifetime.start/end intrinsics to declare
  // that Buf is only used for the duration of the function call, so
  // that the stack space can be reused.

  // Copy variable arguments into buffer.
  int Index = 0;
  for (std::vector<Value*>::iterator Iter = VarArgs.begin();
       Iter != VarArgs.end();
       ++Iter, ++Index) {
    std::vector<Value*> Indexes;
    Indexes.push_back(ConstantInt::get(*Context, APInt(32, 0)));
    Indexes.push_back(ConstantInt::get(*Context, APInt(32, Index)));
    Value *Ptr = GetElementPtrInst::Create(Buf, Indexes, "vararg_ptr", Call);
    new StoreInst(*Iter, Ptr, Call);
  }

  // Cast function to new type to add our extra pointer argument.
  std::vector<Type*> ArgTypes(FuncType->param_begin(), FuncType->param_end());
  ArgTypes.push_back(VarArgsTy->getPointerTo());
  FunctionType *NFTy = FunctionType::get(FuncType->getReturnType(),
                                         ArgTypes, false);
  Value *CastFunc = new BitCastInst(Call->getCalledValue(),
                                    NFTy->getPointerTo(), "vararg_func", Call);

  // Create the converted function call.
  FixedArgs.push_back(Buf);
  Value *NewCall = CallInst::Create(CastFunc, FixedArgs, "", Call);
  NewCall->takeName(Call);
  Call->replaceAllUsesWith(NewCall);
  Call->eraseFromParent();

  return true;
}

bool ExpandVarArgs::runOnModule(Module &M) {
  bool Changed = false;
  TargetData DataLayout(&M);

  for (Module::iterator Iter = M.begin(), E = M.end(); Iter != E; ) {
    Function *Func = Iter++;

    for (Function::iterator BB = Func->begin(), E = Func->end();
         BB != E;
         ++BB) {
      for (BasicBlock::iterator Iter = BB->begin(), E = BB->end();
           Iter != E; ) {
        Instruction *Inst = Iter++;
        if (VAArgInst *VI = dyn_cast<VAArgInst>(Inst)) {
          Changed = true;
          ExpandVAArgInst(VI, &DataLayout);
        } else if (CallInst *Call = dyn_cast<CallInst>(Inst)) {
          Changed |= ExpandVarArgCall(Call);
        }
      }
    }

    if (Func->isVarArg()) {
      Changed = true;
      ExpandVarArgFunc(Func);
    }
  }

  return Changed;
}

ModulePass *createExpandVarArgsPass() {
  return new ExpandVarArgs();
}
