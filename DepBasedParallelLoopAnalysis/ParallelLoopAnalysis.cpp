// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]

#include "ParallelLoopAnalysis.h"

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils/LoopUtils.h>

using namespace llvm;
using namespace lge;

bool ParallelLoopAnalysis::canParallelize(llvm::Loop* L) {
  return (CantParallelize.count(L) == 0);
}

void ParallelLoopAnalysis::inspectMemoryDependence(Dependence &D,
  Instruction &Src, Instruction &Dst) {

  // We don't care for dependences between loads, as they are not "true"
  // dependences.
  if (isa<LoadInst>(Src) && isa<LoadInst>(Dst))
    return;

  // A confused dependence means we don't know how these instructions interact.
  // Thus, we can't parallelize any loop that contains both instructions.
  if (D.isConfused()) {
    unsigned SrcDepth = LI->getLoopDepth(Src.getParent());
    unsigned DstDepth = LI->getLoopDepth(Dst.getParent());
    const Loop *SrcLoop = LI->getLoopFor(Src.getParent());
    const Loop *DstLoop = LI->getLoopFor(Dst.getParent());

    // Align both pointers in the loop tree.
    while (SrcDepth > DstDepth) {SrcLoop = SrcLoop->getParentLoop(); --SrcDepth;}
    while (DstDepth > SrcDepth) {DstLoop = DstLoop->getParentLoop(); --DstDepth;}

    // Find the closest common ancestor.
    unsigned CommonDepth = SrcDepth;

    while (SrcLoop != DstLoop && CommonDepth > 0) {
      SrcLoop = SrcLoop->getParentLoop();
      DstLoop = DstLoop->getParentLoop();
      --CommonDepth;
    }

    const Loop *CommonLoop = SrcLoop;

    // Register all common loops as not parallelizable.
    while (CommonDepth > 0) {
      CantParallelize.insert(CommonLoop);
      CommonLoop = CommonLoop->getParentLoop();
      --CommonDepth;
    }

    return;
  }

  // At this point we have a dependence with some useful info.
  unsigned Level = D.getLevels();
  const Loop *LoopIt = LI->getLoopFor(Src.getParent());

  // Align to the closest common ancestor.
  while(LoopIt && LoopIt->getLoopDepth() > Level)
    LoopIt = LoopIt->getParentLoop();

  // Register each loop in which the two instructions depend on each other.
  while (Level > 0) {
    const SCEV *Distance = D.getDistance(Level);

    // There's no dependence in a level when the dependence distance is known to
    // be zero.
    bool DependenceFree = Distance && isa<SCEVConstant>(Distance) &&
      (cast<SCEVConstant>(Distance)->getValue()->isZero());

    if (!DependenceFree)
      CantParallelize.insert(LoopIt);

    LoopIt = LoopIt->getParentLoop();
    --Level;
  }
}

void ParallelLoopAnalysis::checkRegisterDependencies(Loop *L)
{
  BasicBlock *Header = L->getHeader();
  ConstantInt *Step = nullptr;
  bool hasBadPHI = false;

  // Check for loop-carried dependencies. The assumption is that constant-stride
  // (induction) PHIs can always be rewritten as a function of threadId.
  for (BasicBlock::iterator I = Header->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);

    if (!isInductionPHI(PN, SE, Step)) {
      CantParallelize.insert(L);
      hasBadPHI = true;
      break;
    }
  }

  // Check that no values produced within the loop are used outside of it. If
  // so, iteration order must be preserved.
  if (!hasBadPHI) {
     SmallVector<BasicBlock *, 4> exitBlocks;
     L->getExitBlocks(exitBlocks);

     for (BasicBlock* exit : exitBlocks)
       // As we are on LCSSA form, a PHI in an exit block means that a value
       // scapes the loop.
       if (isa<PHINode>(exit->begin())) {
         CantParallelize.insert(L);
         break;
       }
  }

  const std::vector<Loop *> &subLoops = L->getSubLoops();

  for (auto SL : subLoops)
    checkRegisterDependencies(SL);
}

bool ParallelLoopAnalysis::runOnFunction(llvm::Function &F) {
  DA = &getAnalysis<DependenceAnalysis>();
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  SE = &getAnalysis<ScalarEvolution>();

  CantParallelize.clear();

  // Check for memory dependecies among every pair of instructions in this function.
  for (auto Src = inst_begin(F), SrcE = inst_end(F); Src != SrcE; ++Src)
    if (Src->mayWriteToMemory() || Src->mayReadFromMemory())
      for (auto Dst = Src, DstE = inst_end(F); Dst != DstE; ++Dst)
        if (Dst->mayWriteToMemory() || Dst->mayReadFromMemory())
          if (auto D = DA->depends(&*Src, &*Dst, true))
            inspectMemoryDependence(*D, *Src, *Dst);

  // Check for register dependencies on each loop.
  for (auto L = LI->begin(), E = LI->end(); L != E; ++L) {
    checkRegisterDependencies(*L);
  }

  return false;
}

void ParallelLoopAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequiredTransitive<DependenceAnalysis>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
  AU.addRequired<ScalarEvolution>();
  AU.addRequiredID(LCSSAID);

  AU.setPreservesAll();
}

// Flag to be used from Clag.
static cl::opt<bool> RunParallelLoopAnalysis(
    "parloops",
    cl::desc("Run detection of parallel loops"),
    cl::init(false), cl::ZeroOrMore);

char ParallelLoopAnalysis::ID = 0;

static void registerParallelLoopAnalysis(const PassManagerBuilder &Builder,
                                            legacy::PassManagerBase &PM) {
  if (!RunParallelLoopAnalysis)
    return;

  PM.add(new ParallelLoopAnalysis());
}

static RegisterStandardPasses
  RegisterParallelLoopAnalysis(PassManagerBuilder::EP_EarlyAsPossible,
                               registerParallelLoopAnalysis);

// Flag to be used from Opt.
INITIALIZE_PASS_BEGIN(ParallelLoopAnalysis, "parallel-loop-analysis",
    "Run detection of parallel loops", true, true);
INITIALIZE_PASS_DEPENDENCY(DependenceAnalysis);
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass);
INITIALIZE_PASS_DEPENDENCY(ScalarEvolution);
INITIALIZE_PASS_DEPENDENCY(LCSSA);
INITIALIZE_PASS_END(ParallelLoopAnalysis, "parallel-loop-analysis",
    "Run detection of parallel loops", true, true)
