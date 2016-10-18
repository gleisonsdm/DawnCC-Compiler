// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]

#include "PtrRangeAnalysis.h"

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/Scalar.h>

using namespace llvm;
using namespace lge;

Value *lge::getPointerOperand(Instruction *Inst) {
  if (LoadInst *Load = dyn_cast<LoadInst>(Inst))
    return Load->getPointerOperand();
  else if (StoreInst *Store = dyn_cast<StoreInst>(Inst))
    return Store->getPointerOperand();
  else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst))
    return GEP->getPointerOperand();

  return 0;
}

bool lge::isInvariant(const Value *Val, const Region *R, LoopInfo *LI,
                      AliasAnalysis *AA) {
  // A reference to function argument or constant value is invariant.
  if (isa<Argument>(Val) || isa<Constant>(Val))
    return true;

  const Instruction *I = dyn_cast<Instruction>(Val);
  if (!I)
    return false;

  if (!R->contains(I))
    return true;

  if (I->mayHaveSideEffects())
    return false;

  // When val is a Phi node, it is likely not invariant. We do not check whether
  // Phi nodes are actually invariant, we assume that Phi nodes are usually not
  // invariant. Recursively checking the operators of Phi nodes would lead to
  // infinite recursion.
  if (isa<PHINode>(*I))
    return false;

  for (const Use &Operand : I->operands())
    if (!isInvariant(Operand, R, LI, AA))
      return false;

  // When the instruction is a load instruction, check that no write to memory
  // in the region aliases with the load.
  if (const LoadInst *LI = dyn_cast<LoadInst>(I)) {
    MemoryLocation Loc = MemoryLocation::get(LI);

    // Check if any basic block in the region can modify the location pointed to
    // by 'loc'.  If so, 'val' is (likely) not invariant in the region.
    for (const BasicBlock *BB : R->blocks())
      if (AA->canBasicBlockModify(*BB, Loc))
        return false;
  }

  return true;
}

bool lge::hasKnownElementSize(Value *BasePtr) {
  Type *BaseTy = BasePtr->getType();

  // Only sequential types have elements.
  if (!isa<SequentialType>(BaseTy))
    return false;

  Type *ElemTy = cast<SequentialType>(BaseTy)->getElementType();

  // Get the innermost type in case o multidimensional pointers.
  while (isa<SequentialType>(ElemTy))
    ElemTy = cast<SequentialType>(ElemTy)->getElementType();

  return ElemTy->isSized();
}

Value *lge::getBasePtrValue(Instruction *Inst, const Region *R, LoopInfo *LI,
                            AliasAnalysis *AA, ScalarEvolution *SE) {
  Value *Ptr = getPointerOperand(Inst);
  Loop *L = LI->getLoopFor(Inst->getParent());
  const SCEV *AccessFunction = SE->getSCEVAtScope(Ptr, L);
  const SCEVUnknown *BasePointer =
      dyn_cast<SCEVUnknown>(SE->getPointerBase(AccessFunction));

  if (!BasePointer)
    return nullptr;

  Value *BasePtrValue = BasePointer->getValue();

  // We can't handle direct address manipulation.
  if (isa<UndefValue>(BasePtrValue) || isa<IntToPtrInst>(BasePtrValue))
    return nullptr;

  // The base pointer can vary within the given region.
  if (!isInvariant(BasePtrValue, R, LI, AA))
    return nullptr;

  return BasePtrValue;
}

// Checks if the target of a call instruction has no memory side-effects.
static bool CanProveSideEffectFree(CallInst *CI) {
  Function *Target = CI->getCalledFunction();

  if (!Target)
    return false;

  for (inst_iterator I = inst_begin(Target), E = inst_end(Target); I != E; ++I)
    if (I->mayWriteToMemory() || I->mayReadFromMemory())
      return false;

  return true;
}

bool PtrRangeAnalysis::collectRangeInfo(Instruction *Inst,
                                        RegionRangeInfo *RegionData,
                                        SCEVRangeBuilder *RangeBuilder) {
  // For call instructions, we can only check that it does not access memory.
  if (CallInst *CI = dyn_cast<CallInst>(Inst)) {
    // Check metadata first, as it's cheaper.
    bool markedAsSafe = (!CI->mayHaveSideEffects() && !CI->doesNotReturn() &&
      CI->doesNotAccessMemory());

    return markedAsSafe || CanProveSideEffectFree(CI);
  }

  // Anything that doesn't manipulate memory is not interesting for us.
  if (!Inst->mayWriteToMemory() && !Inst->mayReadFromMemory())
    return !isa<AllocaInst>(Inst);

  // We don't know hot to determine the side-effects of this instruction.
  if (!isa<LoadInst>(Inst) && !isa<StoreInst>(Inst))
    return false;

  // At this point we have a load or a store.
  Value *BasePtrValue = getBasePtrValue(Inst, RegionData->R, LI, AA, SE);

  // We need full type size info.
  if (!BasePtrValue || !hasKnownElementSize(BasePtrValue))
    return false;

  // Extract the access expression.
  Value *Ptr = getPointerOperand(Inst);
  Loop *L = LI->getLoopFor(Inst->getParent());
  const SCEV *AccessFunction = SE->getSCEVAtScope(Ptr, L);

  if (!RangeBuilder->canComputeBoundsFor(AccessFunction))
    return false;

  // Store data for this access.
  if (!RegionData->BasePtrsData.count(BasePtrValue))
    RegionData->BasePtrsData[BasePtrValue] =
        RegionRangeInfo::PtrRangeInfo(BasePtrValue);

  RegionData->BasePtrsData[BasePtrValue].AccessInstructions.push_back(Inst);
  RegionData->BasePtrsData[BasePtrValue].AccessFunctions.push_back(
      AccessFunction);

  return true;
}

void PtrRangeAnalysis::collectRangeInfo(Region *R) {
  RegionRangeInfo RegionData(R);

  // All bounds are computed regarding the region entry.
  Instruction *InsertPt = R->getEntry()->getFirstNonPHI();
  SCEVRangeBuilder RangeBuilder(SE, CurrentFn->getParent()->getDataLayout(), AA,
                                LI, DT, R, InsertPt);

  RegionData.HasFullSideEffectInfo = true;

  for (BasicBlock *BB : R->blocks())
    for (auto I = BB->begin(), E = --BB->end(); I != E; ++I)
      if (!collectRangeInfo(I, &RegionData, &RangeBuilder))
        RegionData.HasFullSideEffectInfo = false;

  RegionsRangeData[R] = RegionData;

  // Collect range info for child regions.
  for (auto &SubRegion : *R)
    collectRangeInfo(&(*SubRegion));
}

bool PtrRangeAnalysis::runOnFunction(llvm::Function &F) {
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  RI = &getAnalysis<RegionInfoPass>().getRegionInfo();
  AA = &getAnalysis<AliasAnalysis>();
  SE = &getAnalysis<ScalarEvolution>();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  CurrentFn = &F;

  releaseMemory();
  collectRangeInfo(RI->getTopLevelRegion());

  return false;
}

void PtrRangeAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequiredID(LoopSimplifyID);
  AU.addRequiredID(LCSSAID);
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
  AU.addRequiredTransitive<ScalarEvolution>();
  AU.addRequired<AliasAnalysis>();
  AU.addRequiredTransitive<RegionInfoPass>();

  AU.setPreservesAll();
}

// Flag to be used from Clang.
static cl::opt<bool>
    RunPtrRangeAnalysis("ptr-ra",
                        cl::desc("Run symbolic pointer range analysis"),
                        cl::init(false), cl::ZeroOrMore);

char PtrRangeAnalysis::ID = 0;

static void registerPtrRangeAnalysis(const PassManagerBuilder &Builder,
                                     legacy::PassManagerBase &PM) {
  if (!RunPtrRangeAnalysis)
    return;

  // Run canonicalization passes before instrumenting, to make the IR simpler.
  // These will only run when invoking directly from Clang.
  PM.add(llvm::createPromoteMemoryToRegisterPass());
  PM.add(llvm::createInstructionCombiningPass());
  PM.add(llvm::createCFGSimplificationPass());
  PM.add(llvm::createReassociatePass());
  PM.add(llvm::createLoopRotatePass());
  PM.add(llvm::createInstructionCombiningPass());

  PM.add(new PtrRangeAnalysis());
}

static RegisterStandardPasses
    RegisterPtrRangeAnalysis(PassManagerBuilder::EP_EarlyAsPossible,
                             registerPtrRangeAnalysis);

// Flag to be used from Opt.
INITIALIZE_PASS_BEGIN(PtrRangeAnalysis, "ptr-range-analysis",
                      "Run symbolic pointer range analysis", true, true);
INITIALIZE_AG_DEPENDENCY(AliasAnalysis);
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass);
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass);
INITIALIZE_PASS_DEPENDENCY(LoopSimplify);
INITIALIZE_PASS_DEPENDENCY(LCSSA);
INITIALIZE_PASS_DEPENDENCY(RegionInfoPass);
INITIALIZE_PASS_DEPENDENCY(ScalarEvolution);
INITIALIZE_PASS_END(PtrRangeAnalysis, "ptr-range-analysis",
                    "Run symbolic pointer range analysis", true, true)
