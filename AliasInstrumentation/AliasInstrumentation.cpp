// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]

#include "AliasInstrumentation.h"
#include "RegionCloneUtil.h"

#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/AliasSetTracker.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include <iostream>
#include <iterator>

using namespace llvm;
using namespace lge;

// Scope selection flags to be used from both Clang and Opt.
static cl::opt<bool>
    RunRegionAliasInstrumentation("region-alias-checks",
                                  cl::desc("Insert region-scoped alias checks"),
                                  cl::init(false), cl::ZeroOrMore);

static cl::opt<bool> RunFunctionAliasInstrumentation(
    "function-alias-checks", cl::desc("Insert function-scoped alias checks"),
    cl::init(false), cl::ZeroOrMore);

static cl::opt<bool> AliasInstrumentationStats(
    "alias-checks-stats", cl::desc("Show DBG stats for alias instrumentation"),
    cl::init(false), cl::ZeroOrMore);

template <typename T>
std::pair<T, T> makeOrderedPair(const T &t1, const T &t2) {
  return (t1 < t2) ? std::make_pair(t1, t2) : std::make_pair(t2, t1);
}

// Checks if at least one of the loops in the region contains a store
// instruction.
static bool hasNestedStore(Region *R, LoopInfo *LI, PtrRangeAnalysis *PtrRA) {
  for (auto &Pair : PtrRA->RegionsRangeData[R].BasePtrsData)
    for (Instruction *Inst : Pair.second.AccessInstructions) {
      Loop *L = LI->getLoopFor(Inst->getParent());

      if (isa<StoreInst>(Inst) && L && R->contains(L))
        return true;
    }

  return false;
}

// Checks if at least one loop is completely contained inside a given region.
static bool regionHasLoop(Region *R, LoopInfo *LI) {
  for (const BasicBlock *BB : R->blocks()) {
    Loop *L = LI->getLoopFor(BB);

    if (L && R->contains(L))
      return true;
  }

  return false;
}

void AliasInstrumentation::fixAliasInfo(Region *R) {
  MDBuilder MDB(CurrentFn->getContext());
  if (!MDDomain)
    MDDomain = MDB.createAnonymousAliasScopeDomain(CurrentFn->getName());
  DenseMap<const Value *, MDNode *> Scopes;
  unsigned PtrCount = 0;

  // Create a different alias scope for each base pointer in the region.
  for (auto &Pair : PtrRA->RegionsRangeData[R].BasePtrsData) {
    const Value *BasePtr = Pair.first;
    std::string Name = CurrentFn->getName();

    Name += (BasePtr->hasName()) ? (": %" + BasePtr->getName().str())
                                 : (": ptr " + utostr(PtrCount++));
    MDNode *Scope = MDB.createAnonymousAliasScope(MDDomain, Name);
    Scopes.insert(std::make_pair(BasePtr, Scope));
  }

  // Set the actual scoped alias tags for each memory instruction in the region.
  // A memory instruction always aliases its base pointer and never aliases
  // other pointers in the region.
  for (auto &Pair : PtrRA->RegionsRangeData[R].BasePtrsData) {
    const Value *BasePtr = Pair.first;

    // Set the alias metadata for each memory access instruction in the region
    // for which we have range info.
    for (auto MemInst : Pair.second.AccessInstructions) {
      // Check that the instruction was not removed from the region.
      if (!MemInst->getParent())
        continue;

      // A memory instruction always aliases its base pointer.
      MemInst->setMetadata(
          LLVMContext::MD_alias_scope,
          MDNode::concatenate(
              MemInst->getMetadata(LLVMContext::MD_alias_scope),
              MDNode::get(CurrentFn->getContext(), Scopes[BasePtr])));

      // The instruction never aliases other pointers in the region.
      for (auto &OtherPair : PtrRA->RegionsRangeData[R].BasePtrsData) {
        const Value *OtherBasePtr = OtherPair.first;

        // Slip the instruction's own base pointer.
        if (OtherBasePtr == BasePtr)
          continue;

        MemInst->setMetadata(
            LLVMContext::MD_noalias,
            MDNode::concatenate(
                MemInst->getMetadata(LLVMContext::MD_noalias),
                MDNode::get(CurrentFn->getContext(), Scopes[OtherBasePtr])));
      }
    }
  }
}

void AliasInstrumentation::registerClonedBlocks(Region *R) {
  for (BasicBlock *BB : R->blocks())
    ClonedBlocks.insert(BB);
}

void AliasInstrumentation::buildNoAliasClone(Region *R, Value *CheckResult) {
  if (!CheckResult)
    return;

  // Collect stats before cloning the region. The number of loops guarded by the
  // checks is the same as the number of loop headers within the region.
  if (AliasInstrumentationStats)
    for (const BasicBlock *BB : R->blocks())
      if (LI->getLoopFor(BB) && LI->getLoopFor(BB)->getHeader() == BB)
        ClonedLoops++;

  Region *ClonedRegion = cloneRegion(R, nullptr, RI, DT, DF);
  registerClonedBlocks(R);
  registerClonedBlocks(ClonedRegion);

  // Build the conditional brach based on the dynamic test result.
  TerminatorInst *Br = R->getEnteringBlock()->getTerminator();
  BuilderType Builder(CurrentFn->getContext(),
                      TargetFolder(CurrentFn->getParent()->getDataLayout()));
  Builder.SetInsertPoint(Br);
  Builder.CreateCondBr(CheckResult, R->getEntry(), ClonedRegion->getEntry());
  Br->eraseFromParent();

  fixAliasInfo(R);
}

Value *AliasInstrumentation::chainChecks(std::vector<Value *> Checks,
                                         BuilderType *Builder) {
  if (Checks.size() < 1)
    return nullptr;

  Value *Rhs = Checks[0];

  for (std::vector<Value *>::size_type I = 1; I != Checks.size(); I++) {
    Rhs = Builder->CreateAnd(Checks[I], Rhs, "region-no-alias");
  }

  return Rhs;
}

Value *AliasInstrumentation::buildRangeCheck(
    Value *BasePtrA, Value *BasePtrB, std::pair<Value *, Value *> BoundsA,
    std::pair<Value *, Value *> BoundsB, BuilderType *Builder,
    SCEVRangeBuilder *RangeBuilder) {
  Value *LowerA = BoundsA.first;
  Value *UpperA = BoundsA.second;
  Value *LowerB = BoundsB.first;
  Value *UpperB = BoundsB.second;

  // Stretch both upper bounds past the last addressable byte.
  UpperA = RangeBuilder->stretchPtrUpperBound(BasePtrA, UpperA);
  UpperB = RangeBuilder->stretchPtrUpperBound(BasePtrB, UpperB);

  // Cast all bounds to i8* (equivalent to void*, according to the LLVM manual).
  Type *I8PtrTy = Builder->getInt8PtrTy();
  LowerA = RangeBuilder->InsertNoopCastOfTo(LowerA, I8PtrTy);
  LowerB = RangeBuilder->InsertNoopCastOfTo(LowerB, I8PtrTy);
  UpperA = RangeBuilder->InsertNoopCastOfTo(UpperA, I8PtrTy);
  UpperB = RangeBuilder->InsertNoopCastOfTo(UpperB, I8PtrTy);

  // Build actual interval comparisons.
  Value *AIsBeforeB = Builder->CreateICmpULE(UpperA, LowerB);
  Value *BIsBeforeA = Builder->CreateICmpULE(UpperB, LowerA);

  Value *Check = Builder->CreateOr(AIsBeforeB, BIsBeforeA, "pair-no-alias");

  return Check;
}

bool
AliasInstrumentation::computePtrsDependence(Region *R,
                                            ValuePairSet *PtrPairsToCheck) {
  AliasSetTracker AST(*AA);

  // We only consider dependencies within the region.
  for (BasicBlock *BB : R->blocks())
    AST.add(*BB);

  for (auto &Pair : PtrRA->RegionsRangeData[R].BasePtrsData) {
    Value *BasePtr = Pair.second.BasePtr;

    // Use the alias metadata of each access instruction.
    for (Instruction *Inst : Pair.second.AccessInstructions) {
      AAMDNodes AAMD;
      Inst->getAAMetadata(AAMD);
      AliasSet &AS =
          AST.getAliasSetForPointer(BasePtr, MemoryLocation::UnknownSize, AAMD);

      // Store all pointers that need to be tested against the current one.
      for (const auto &AliasPointer : AS) {
        Value *AliasingPtr = AliasPointer.getValue();

        // We only check against pointers for which we have range info.
        if (!PtrRA->RegionsRangeData[R].BasePtrsData.count(AliasingPtr) ||
            (BasePtr == AliasingPtr))
          continue;

        // Guarantees ordered pairs (avoids repetition).
        auto Dep = makeOrderedPair(BasePtr, AliasingPtr);
        PtrPairsToCheck->insert(Dep);
      }
    }
  }
}

void AliasInstrumentation::buildSCEVBounds(Region *R,
                                           SCEVRangeBuilder *RangeBuilder,
                                           BoundMap *PointerBounds) {
  // Compute access bounds for each base pointer analysed in the region.
  for (auto &Pair : PtrRA->RegionsRangeData[R].BasePtrsData) {
    Value *Low = RangeBuilder->getULowerBound(Pair.second.AccessFunctions);
    Value *Up = RangeBuilder->getUUpperBound(Pair.second.AccessFunctions);

    assert((Low && Up) &&
           "All access expressions should have computable SCEV bounds by now");

    PointerBounds->insert(std::make_pair(Pair.first, std::make_pair(Low, Up)));
  }
}

BasicBlock *AliasInstrumentation::splitEdge(BasicBlock *Src, BasicBlock *Dst) {
  TerminatorInst *TI = Src->getTerminator();
  int SuccNum = -1;

  // Get the successor index of Dst.
  for (unsigned i = 0; i < TI->getNumSuccessors(); i++)
    if (TI->getSuccessor(i) == Dst)
      SuccNum = i;

  assert(SuccNum >= 0 && "Trying to split an edge that doesn't exist!");

  // Create a new basic block, linking it into the CFG.
  BasicBlock *NewBB = BasicBlock::Create(TI->getContext(),
      Src->getName() + "." + Dst->getName() + ".split_edge");

  BranchInst *NewBI = BranchInst::Create(Dst, NewBB);
  NewBI->setDebugLoc(TI->getDebugLoc());
  TI->setSuccessor(SuccNum, NewBB);

  // Insert the block into the function, right after Src.
  Function::iterator FBBI = Src;
  CurrentFn->getBasicBlockList().insert(++FBBI, NewBB);

  // If there are any PHI nodes in Dst, we need to update them so that they
  // merge incoming values from NewBB instead of from Src.
  for (BasicBlock::iterator I = Dst->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);
    PN->setIncomingBlock(PN->getBasicBlockIndex(Src), NewBB);
  }

  SmallVector<BasicBlock*, 8> OtherPreds;

  // Collect the remaining predecessors of Dst.
  for (pred_iterator I = pred_begin(Dst), E = pred_end(Dst); I != E; ++I)
    if ((BasicBlock*)*I != NewBB)
      OtherPreds.push_back(*I);

  // Update DominatorTree information
  DomTreeNode *SrcNode = DT->getNode(Src);
  DomTreeNode *DstNode = DT->getNode(Dst);

  if (!SrcNode || !DstNode)
    return NewBB;

  DomTreeNode *NewBBNode = DT->addNewBlock(NewBB, Src);
  bool NewBBDominatesDst = true;

  // Check if there's a path to Dst through another predecessor that doesn't
  // pass through NewBB.
  while (!OtherPreds.empty() && NewBBDominatesDst) {
    if (DomTreeNode *OPNode = DT->getNode(OtherPreds.back()))
      NewBBDominatesDst = DT->dominates(DstNode, OPNode);

    OtherPreds.pop_back();
  }

  if (NewBBDominatesDst)
    DT->changeImmediateDominator(DstNode, NewBBNode);

  return NewBB;
}

void AliasInstrumentation::groupPredecessors(BasicBlock *BB,
    std::function<bool(BasicBlock*)> hasProperty,
    std::function<void(BasicBlock*)> changeListener) {

  int NumPreds = distance(pred_begin(BB), pred_end(BB));
  SmallVector<BasicBlock *, 4> PredToSplit;

  for (auto PI = pred_begin(BB), PE = pred_end(BB); PI != PE; ++PI)
    if (hasProperty(*PI))
      PredToSplit.push_back(*PI);

  // If all or none of the predecessors follow the property, we don't need to
  // split them, just the block itself.
  if ((PredToSplit.size() == 0) || (PredToSplit.size() == NumPreds))
    changeListener(SplitBlock(BB, BB->begin(), DT, LI));
  else
    SplitBlockPredecessors(BB, PredToSplit, ".region", AA, DT, LI);
}

void AliasInstrumentation::simplifyRegion(Region *R) {
  // If this is a top-level region, create an exit block for it.
  if (!R->getExit()) {
    BasicBlock *Exiting = getFnExitingBlock();
    assert(Exiting && "Candidate top-level regions need an exiting block");
    R->replaceExitRecursive(SplitBlock(Exiting, Exiting->getTerminator(),
        DT, LI));
  }

  // If the region doesn't have an entering block, create one by making all
  // outside predecessors fall into a single block before the entry.
  if (!R->getEnteringBlock())
    groupPredecessors(R->getEntry(),
        [&](BasicBlock *BB){ return !R->contains(BB); },
        [&](BasicBlock *BB){ R->replaceEntryRecursive(BB); });

  // Split the entry edge, so that checks will be in a single block.
  splitEdge(R->getEnteringBlock(), R->getEntry());

  // If the region doesn't have an exiting block, create one by making all
  // internal predecessors fall into a single block before the exit.
  if (!R->getExitingBlock())
    groupPredecessors(R->getExit(),
        [&](BasicBlock *BB){ return R->contains(BB); },
        [&](BasicBlock *BB){ R->replaceExitRecursive(BB); });
}

Value *AliasInstrumentation::insertDynamicChecks(Region *R) {
  ValuePairSet PtrPairsToCheck;
  computePtrsDependence(R, &PtrPairsToCheck);

  // If there are no conflicting pointers, don't instrument anything.
  if (PtrPairsToCheck.size() == 0)
    return nullptr;

  // Create an entering block to receive the checks.
  simplifyRegion(R);

  // Set instruction insertion context. We'll insert the run-time tests in the
  // region entering block.
  Instruction *InsertPt = R->getEnteringBlock()->getTerminator();
  SCEVRangeBuilder RangeBuilder(SE, CurrentFn->getParent()->getDataLayout(), AA,
      LI, DT, R, InsertPt);
  BuilderType Builder(CurrentFn->getContext(),
                      TargetFolder(CurrentFn->getParent()->getDataLayout()));
  Builder.SetInsertPoint(InsertPt);

  BoundMap PointerBounds;
  buildSCEVBounds(R, &RangeBuilder, &PointerBounds);

  std::vector<Value *> PairwiseChecks;

  // Insert comparison expressions for every pair of pointers that need to be
  // checked in the region.
  for (auto &Pair : PtrPairsToCheck) {
    auto Check =
        buildRangeCheck(Pair.first, Pair.second, PointerBounds[Pair.first],
                        PointerBounds[Pair.second], &Builder, &RangeBuilder);
    PairwiseChecks.push_back(Check);
  }

  // Combine all checks into a single boolean result using AND.
  return chainChecks(PairwiseChecks, &Builder);
}

BasicBlock *AliasInstrumentation::getFnExitingBlock() {
  std::vector<BasicBlock *> ReturnBlocks;

  for (auto I = CurrentFn->begin(), E = CurrentFn->end(); I != E; ++I)
    if (isa<ReturnInst>(I->getTerminator()))
      ReturnBlocks.push_back(I);

  if (ReturnBlocks.size() != 1)
    return nullptr;

  return ReturnBlocks.front();
}

bool AliasInstrumentation::canInstrument(Region *R) {
  // If we have only one pointer, there are no alias conflicts.
  if (PtrRA->RegionsRangeData[R].BasePtrsData.size() < 2)
    return false;

  // If there's no exit, then we can't merge cloned regions.
  if (!R->getExit() && !getFnExitingBlock())
    return false;

  // It's not worth instrumenting regions that have no loops: the checks
  // wouldn't pay for themselves.
  if (!regionHasLoop(R, LI))
    return false;

  // Regions where we can only disambiguate loads are usually not profitable,
  // since load-load dependencies are not a problem for most optimizations.
  if (!hasNestedStore(R, LI, PtrRA))
    return false;

  // We can't instrument a region that overlaps another already instrumented.
  // This would cause blocks to be cloned twice and a mess on PHI nodes.
  for (BasicBlock *BB : R->blocks())
    if (ClonedBlocks.find(BB) != ClonedBlocks.end())
      return false;

  return true;
}

void AliasInstrumentation::instrumentRegion(Region *R) {
  if (RunFunctionAliasInstrumentation && !canInstrument(R))
    return;

  // In region-scoped mode, if a given region can't be instrumented, we try its
  // children (only instrument regions for which full range info is available).
  if (RunRegionAliasInstrumentation &&
     (!canInstrument(R) || !PtrRA->RegionsRangeData[R].HasFullSideEffectInfo)) {

    // Traverse children in reverse order, so we reach dominated regions first.
    for (auto SubRegion = R->end(); SubRegion != R->begin();) {
      --SubRegion;
      instrumentRegion(&(**SubRegion));
    }

    return;
  }

  Value *CheckResult = insertDynamicChecks(R);
  buildNoAliasClone(R, CheckResult);
}

bool AliasInstrumentation::runOnFunction(llvm::Function &F) {
  // Collect all analyses needed for runtime check generation.
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  RI = &getAnalysis<RegionInfoPass>().getRegionInfo();
  AA = &getAnalysis<AliasAnalysis>();
  SE = &getAnalysis<ScalarEvolution>();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  DF = &getAnalysis<DominanceFrontier>();
  PtrRA = &getAnalysis<PtrRangeAnalysis>();

  CurrentFn = &F;

  releaseMemory();
  instrumentRegion(RI->getTopLevelRegion());

  // Print final stats.
  if (AliasInstrumentationStats) {
    size_t TotalLoops = 0;

    // Get number of loops in the function.
    for (Function::iterator BBIt = F.begin(), BE = F.end(); BBIt != BE; ++BBIt) {
      BasicBlock* BB = BBIt;

      if (LI->getLoopFor(BB) && LI->getLoopFor(BB)->getHeader() == BB)
        TotalLoops++;
    }

    if (TotalLoops > 0)
      std::cerr << "[RESTRICTIFICATION] function: " << std::string(F.getName()) <<
        ", total-loops: " << TotalLoops << ", restrictified-loops: " <<
        ClonedLoops << std::endl;
  }

  return true;
}

void AliasInstrumentation::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<DominanceFrontier>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<ScalarEvolution>();
  AU.addRequired<AliasAnalysis>();
  AU.addRequired<RegionInfoPass>();
  AU.addRequired<PtrRangeAnalysis>();

  // Changing the CFG like we do doesn't preserve anything.
}

char AliasInstrumentation::ID = 0;

static void registerAliasInstrumentation(const PassManagerBuilder &Builder,
                                         legacy::PassManagerBase &PM) {
  if (!RunRegionAliasInstrumentation && !RunFunctionAliasInstrumentation)
    return;

  PM.add(new AliasInstrumentation());
}

static RegisterStandardPasses
    RegisterAliasInstrumentation(PassManagerBuilder::EP_EarlyAsPossible,
                                 registerAliasInstrumentation);

// Flag to be used from Opt.
INITIALIZE_PASS_BEGIN(AliasInstrumentation, "alias-instrumentation",
                      "Insert alias checks and clone regions", false, false);
INITIALIZE_AG_DEPENDENCY(AliasAnalysis);
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass);
INITIALIZE_PASS_DEPENDENCY(DominanceFrontier);
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass);
INITIALIZE_PASS_DEPENDENCY(RegionInfoPass);
INITIALIZE_PASS_DEPENDENCY(ScalarEvolution);
INITIALIZE_PASS_DEPENDENCY(PtrRangeAnalysis);
INITIALIZE_PASS_END(AliasInstrumentation, "alias-instrumentation",
                    "Insert alias checks and clone regions", false, false)
