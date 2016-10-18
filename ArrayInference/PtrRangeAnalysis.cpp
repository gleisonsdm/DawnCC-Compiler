// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]

#include "PtrRangeAnalysis.h"

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/Scalar.h>
#include "llvm/ADT/Statistic.h"

using namespace llvm;
using namespace lge;

#define DEBUG_TYPE "PTRRangeAnalysis"
#define LOAD 1
#define STORE 2
#define LOADSTORE 3

STATISTIC(numMA , "Number of memory access"); 
STATISTIC(numAMA , "Number of memory analyzed access");
STATISTIC(numAA , "Number of arrays"); 
STATISTIC(numAAA , "Number of analyzed arrays");

static cl::opt<bool> Cllicm("Ptr-licm",                      
    cl::desc("Use loop invariant code motion in Pointer Range Analysis.")); 
    
static cl::opt<bool> Clregion("Ptr-region",                      
    cl::desc("Rebuild regions in Pointer Range Analysis")); 

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
  if (!isInvariant(BasePtrValue, R, LI, AA)) {
    return nullptr;
  }

  // If base pointer is the pointer operand, return it.
  return BasePtrValue;
}

char PtrRangeAnalysis::getPointerAcessType (Loop *L, Value *V) {
  if (PointerAcess.count(L) &&
      PointerAcess[L].count(V))
    return PointerAcess[L][V];
  return LOADSTORE;
}

char PtrRangeAnalysis::getPointerAcessType (Region *R, Value *V) {
  if (PointerAcessRegion.count(R) &&
      PointerAcessRegion[R].count(V))
    return PointerAcessRegion[R][V];
  return LOADSTORE;
}

void PtrRangeAnalysis::analyzeLoopPointers (Loop *L) {
  for (auto BB = L->block_begin(), BE = L->block_end(); BB != BE; BB++) {
    for (auto I = (*BB)->begin(), IE = (*BB)->end(); I != IE; I++) {
      if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
        Value *BasePtrV = getPointerOperand(I);
        while (isa<LoadInst>(BasePtrV) || isa<GetElementPtrInst>(BasePtrV)) {
          if (LoadInst *LD = dyn_cast<LoadInst>(BasePtrV))
            BasePtrV = LD->getPointerOperand();
          if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(BasePtrV))
            BasePtrV = GEP->getPointerOperand();
        }
        if (isa<LoadInst>(I))
          PointerAcess[L][BasePtrV] |= LOAD;
        if (isa<StoreInst>(I))
          PointerAcess[L][BasePtrV] |= LOADSTORE;
      }
    }
  }
}

void PtrRangeAnalysis::analyzeRegionPointers (Region *R) {
  for (auto BB = R->block_begin(), BE = R->block_end(); BB != BE; BB++) {
    for (auto I = (*BB)->begin(), IE = (*BB)->end(); I != IE; I++) {
      if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
        Value *BasePtrV = getPointerOperand(I);
        while (isa<LoadInst>(BasePtrV) || isa<GetElementPtrInst>(BasePtrV)) {
          if (LoadInst *LD = dyn_cast<LoadInst>(BasePtrV))
            BasePtrV = LD->getPointerOperand();
          if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(BasePtrV))
            BasePtrV = GEP->getPointerOperand();
        }
        if (isa<LoadInst>(I))
          PointerAcessRegion[R][BasePtrV] |= LOAD;
        if (isa<StoreInst>(I))
          PointerAcessRegion[R][BasePtrV] |= LOADSTORE;
      }
    }
  }
}

bool PtrRangeAnalysis::isSafeCallInst (CallInst *CI) {
  if (CI->doesNotReturn())
    return false;

  if (CI->doesNotAccessMemory() && !CI->mayHaveSideEffects())
    return true;
  
  Function *F = CI->getCalledFunction();
  if (!F) {
    return false;
  }

  if (ValidFunctions.count(F) != 0)
    return ValidFunctions[F];

  // If the function is just a declaration, try to identify
  // knowed functions.
  if (F->isDeclaration()) {
    std::string tmp = F->getName();
    // Rand function.
    if (tmp == "rand" ||
    // This functions can be generated by LLVM's IR.
        tmp == "llvm.memcpy.p0i8.p0i8.i32" || 
        tmp == "llvm.memcpy.p0i8.p0i8.i64" ||
    // This functions are present in math.h library.
        tmp == "cos" || tmp == "sin" || tmp == "tan" || tmp == "acos" ||
        tmp == "asin" || tmp == "atan" || tmp == "atan2" || tmp == "cosh" ||
        tmp == "sinh" || tmp == "tanh" || tmp == "acosh" || tmp == "asinh" ||
        tmp == "atanh" || tmp == "atanh" || tmp == "exp" || tmp == "frexp" ||
        tmp == "ldexp"|| tmp == "log" || tmp == "log10" || tmp == "modf" ||
        tmp == "exp2" || tmp == "expm1" || tmp == "ilogb" || tmp == "log1p" ||
        tmp == "log2" || tmp == "logb" || tmp == "scalbn" || tmp == "scalbln" ||
        tmp == "pow" || tmp == "sqrt" || tmp == "cbrt" || tmp == "hypot" ||
        tmp == "erf" || tmp == "erfc" || tmp == "tgamma" || tmp == "lgamma" ||
        tmp == "ceil" || tmp == "floor" || tmp == "fmod" || tmp == "trunc" ||
        tmp == "round" || tmp == "lround" || tmp == "llround" ||
        tmp == "rint" || tmp == "lrint" || tmp == "llrint" ||
        tmp == "nearbyint" || tmp == "remainder" || tmp == "remquo" ||
        tmp == "copysig" "" || tmp == "nan" || tmp == "nextafter" ||
        tmp == "nexttoward" || tmp == "fdim" || tmp == "fmax" ||
        tmp == "fmin" || tmp == "fabs" || tmp == "abs" || tmp == "fma") {
      ValidFunctions[F] = true;
      return true;
    }
    ValidFunctions[F] = false;
    return false;
  }

  if (F->isIntrinsic()) {
    ValidFunctions[F] = false;
    return false;
  }

  // If the function is not declaration or intrinsic, we can iterate in the
  // instructions. In this case, search for possible affects in memory.
  
  auto TyID = F->getReturnType()->getTypeID();

  if (TyID != Type::HalfTyID && TyID != Type::FloatTyID &&
      TyID != Type::DoubleTyID && TyID != Type::X86_FP80TyID &&
      TyID != Type::PPC_FP128TyID && TyID != Type::X86_MMXTyID &&
      TyID != Type::IntegerTyID && TyID != Type::VectorTyID) {
    ValidFunctions[F] = false;
    return false;
  }

  // Return false if some argument is not a primitive type.
  for (auto Arg = F->arg_begin(), ArgE = F->arg_end(); Arg != ArgE; Arg++) {
    TyID = Arg->getType()->getTypeID();
    if (TyID == Type::HalfTyID || TyID == Type::FloatTyID ||
        TyID == Type::DoubleTyID || TyID == Type::X86_FP80TyID ||
        TyID == Type::PPC_FP128TyID || TyID == Type::X86_MMXTyID ||
        TyID == Type::IntegerTyID || TyID == Type::VectorTyID)
      continue;
    ValidFunctions[F] = false;
    return false;
  }

  // Search for Global Values uses in all instructions present in the function.
  for (auto B = F->begin(), BE = F->end(); B != BE; B++)
    for (auto I = B->begin(), IE = B->end(); I != IE; I++) {
      if (LoadInst *LD = dyn_cast<LoadInst>(I))
        if (isa<GlobalValue>(LD->getPointerOperand())) {
          ValidFunctions[F] = false;
          return false;
        }
      if (StoreInst *ST = dyn_cast<StoreInst>(I))
        if (isa<GlobalValue>(ST->getPointerOperand())) {
          ValidFunctions[F] = false;
          return false;
        }
      if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I))
        if (isa<GlobalValue>(GEP->getPointerOperand())) {
          ValidFunctions[F] = false;
          return false;
        }
    }
  ValidFunctions[F] = true;
  return true;
}

bool lge::isPresentOnLoop (Instruction *Inst, Loop *L) {
  // Verify if the instruction is present in the loop L.
  for (Loop::block_iterator B = L->block_begin(), BE = L->block_end();
       B != BE; B++)
    for (auto I = (*B)->begin(), IE = (*B)->end(); I != IE; I++)
      if (Instruction *Inst2 = dyn_cast<Instruction>(I))
        if (Inst2 == Inst)
          return true;
  return false;
}

bool lge::insertOperandsRec(Instruction *I, Loop *L,
                            std::vector<Instruction*> & instVec) {
  if (isa<PHINode>(I))
    return false;
  if (isa<AllocaInst>(I) && isPresentOnLoop(I, L)) {
    instVec.push_back(I);
    return true;
  }
  for (int i = 0, ie = I->getNumOperands(); i != ie; i++) {
    if (isa<PHINode>(I->getOperand(i)) || isa<GlobalValue>(I->getOperand(i)))
      return false;
    if (Instruction *Inst = dyn_cast<Instruction>(I->getOperand(i))) {
      if (isPresentOnLoop(Inst, L))
        if (!insertOperandsRec(Inst, L, instVec))
          return false;
    }
  }
  if (!isa<AllocaInst>(I))
    instVec.push_back(I);
  return true;
}

void PtrRangeAnalysis::tryOptimizeLoop(Loop *L) {
  analyzeLoopPointers(L);
  std::vector<Instruction*> instVec;
  BasicBlock *BB = L->getLoopPreheader();
  if (!BB)
    return;
  bool stats = true;
  // Find invariant Loads and GetElementPtrs to try change its location.
  // We need to do several modifications in IR, to try remove some
  // alias of the code, trying create a better code to try estimate the
  // bounds of pointers insite this loop.
  for (Loop::block_iterator B = L->block_begin(), BE = L->block_end();
       B != BE; B++) {
    for (auto I = (*B)->begin(), IE = (*B)->end(); I != IE; I++) {
      if (!L->hasLoopInvariantOperands(I))
        continue;
      if (!isa<LoadInst>(I) && !isa<GetElementPtrInst>(I))
        continue;
      if (isa<LoadInst>(I) && !insertInvariantLoadRange(I))
        continue;
      stats = (insertOperandsRec(I, L, instVec) & stats);
    }
  }
  Instruction *Inst = BB->getTerminator();
  if (stats) {
    bool MoveInsertPt = false;
    for (int i = 0, ie = instVec.size(); i < ie; i++) {
      if (instVec[i] == Inst)
        MoveInsertPt = true;
      instVec[i]->moveBefore(Inst);
    }
    if (MoveInsertPt) {
       Instruction *InstTmp = BB->getFirstInsertionPt();
       Inst->moveBefore(InstTmp);
    }
  }
}

void PtrRangeAnalysis::tryOptimizeFunction(Function *F, LoopInfo *LI) {
  std::map <Loop*, bool> loops;
  // Collect all Loops present in the function "F".
  for (auto B = F->begin(), BE = F->end(); B != BE; B++) {
    Loop *L = LI->getLoopFor(B);
    if (L)
      loops[L] = true;
  }
  // try optimize the collected loops
  auto I = loops.end(), IE = loops.begin();
  while(I != IE) {
    I--;
    tryOptimizeLoop(I->first);
  }
}

bool PtrRangeAnalysis::insertInvariantLoadRange (Instruction *Inst) {
  if (!isa<LoadInst>(Inst))
    return false;

  BasicBlock *BB = Inst->getParent();
  Region *R = RI->getRegionFor(BB); 
  RegionRangeInfo RegionData(R);

  // All bounds are computed regarding the region entry.
  Instruction *InsertPt = R->getEntry()->getFirstNonPHI();
  
  SCEVRangeBuilder RangeBuilder(SE, CurrentFn->getParent()->getDataLayout(), AA,
                                LI, DT, R, InsertPt);

  // At this point we have a load or a store.
  Value *BasePtrValue = getPointerOperand(Inst);

  // We need full type size info.
  if (!BasePtrValue || !hasKnownElementSize(BasePtrValue))
    return false;

  // Extract the access expression.
  Value *Ptr = getPointerOperand(Inst);
  Loop *L = LI->getLoopFor(Inst->getParent());
  const SCEV *AccessFunction = SE->getSCEVAtScope(Ptr, L);

  if (!RangeBuilder.canComputeBoundsFor(AccessFunction))
    return false;

  Value *BasePtrV = BasePtrValue;
  while (isa<LoadInst>(BasePtrV) || isa<GetElementPtrInst>(BasePtrV)) {
    if (LoadInst *LD = dyn_cast<LoadInst>(BasePtrV))
      BasePtrV = LD->getPointerOperand();
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(BasePtrV))
      BasePtrV = GEP->getPointerOperand();
  }
  
  if (!BasePtrV)
    return false;

  // Store data for this access.
  if (!RegionData.BasePtrsData.count(BasePtrV))
    RegionData.BasePtrsData[BasePtrV] =
        RegionRangeInfo::PtrRangeInfo(BasePtrV);

  RegionData.BasePtrsData[BasePtrV].AccessInstructions.push_back(Inst);
  RegionData.BasePtrsData[BasePtrV].AccessFunctions.push_back(
      AccessFunction);
  
  numAMA++;
  return true;
}

bool PtrRangeAnalysis::collectRangeInfo(Instruction *Inst,
                                        RegionRangeInfo *RegionData,
                                        SCEVRangeBuilder *RangeBuilder) {
  // For call instructions, we can only check that it does not access memory.
  if (CallInst *CI = dyn_cast<CallInst>(Inst))
    return isSafeCallInst(CI);
    //return (!CI->mayHaveSideEffects() && !CI->doesNotReturn() &&
    //        CI->doesNotAccessMemory());

  // Anything that doesn't manipulate memory is not interesting for us.
  if (!Inst->mayWriteToMemory() && !Inst->mayReadFromMemory())
    return !isa<AllocaInst>(Inst);
  
  // We don't know hot to determine the side-effects of this instruction.
  if (!isa<LoadInst>(Inst) && !isa<StoreInst>(Inst))
    return false;

  numMA++;

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

  Value *BasePtrV = BasePtrValue;
  while (isa<LoadInst>(BasePtrV) || isa<GetElementPtrInst>(BasePtrV)) {
    if (LoadInst *LD = dyn_cast<LoadInst>(BasePtrV))
      BasePtrV = LD->getPointerOperand();
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(BasePtrV))
      BasePtrV = GEP->getPointerOperand();
  }
  
  if (!BasePtrV)
    return false;

  // Store data for this access.
  if (!RegionData->BasePtrsData.count(BasePtrV))
    RegionData->BasePtrsData[BasePtrV] =
        RegionRangeInfo::PtrRangeInfo(BasePtrV);

  RegionData->BasePtrsData[BasePtrV].AccessInstructions.push_back(Inst);
  RegionData->BasePtrsData[BasePtrV].AccessFunctions.push_back(
      AccessFunction);
  
  numAMA++;
  return true;
}

void PtrRangeAnalysis::analyzeReducedRegion (Region *R) {
  if (RegionsRangeData[R].HasFullSideEffectInfo)
    return;

  Region *Rr = RR->returnReducedRegion(R);
  if (!Rr)
    return;

  RegionRangeInfo RegionData(Rr);

  analyzeRegionPointers(Rr);

  // All bounds are computed regarding the region entry.
  Instruction *InsertPt = Rr->getEntry()->getFirstNonPHI();
  SCEVRangeBuilder RangeBuilder(SE, CurrentFn->getParent()->getDataLayout(), AA,
                                LI, DT, Rr, InsertPt);

  RegionData.HasFullSideEffectInfo = true;
  
  // Call Instructions may cannot be safetly.
  for (BasicBlock *BB : R->blocks())
    for (auto I = BB->begin(), E = --BB->end(); I != E; ++I) {
      if (CallInst *CI = dyn_cast<CallInst>(I)) 
        if (isSafeCallInst(CI)) {
          RegionData.HasFullSideEffectInfo = false;
        }
  }
  if (RegionData.HasFullSideEffectInfo == true) {
    for (BasicBlock *BB : Rr->blocks())
      for (auto I = BB->begin(), E = --BB->end(); I != E; ++I) {
        if (collectRangeInfo(I, &RegionData, &RangeBuilder) == false) {
          RegionData.HasFullSideEffectInfo = false;
        }
      }
  }
  RegionsRangeData[Rr] = RegionData;
}

void PtrRangeAnalysis::collectRangeInfo(Region *R) {
  RegionRangeInfo RegionData(R);

  analyzeRegionPointers(R);
  // All bounds are computed regarding the region entry.
  Instruction *InsertPt = R->getEntry()->getFirstNonPHI();
  SCEVRangeBuilder RangeBuilder(SE, CurrentFn->getParent()->getDataLayout(), AA,
                                LI, DT, R, InsertPt);

  RegionData.HasFullSideEffectInfo = true;
  
  for (BasicBlock *BB : R->blocks())
    for (auto I = BB->begin(), E = --BB->end(); I != E; ++I) {
      if (collectRangeInfo(I, &RegionData, &RangeBuilder) == false) {
        RegionData.HasFullSideEffectInfo = false;
      }
    }
  RegionsRangeData[R] = RegionData;
  
  if (!RegionData.HasFullSideEffectInfo && Clregion)
    analyzeReducedRegion(R);
 
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
  RR = &getAnalysis<RegionReconstructor>();

  CurrentFn = &F;

  if (Cllicm)
    tryOptimizeFunction(&F, LI);

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
  AU.addRequired<RegionReconstructor>();

  AU.setPreservesAll();
}

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
