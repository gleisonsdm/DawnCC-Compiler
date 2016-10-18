// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]
//
// Instrument regions with runtime checks capable of verifying if there are true
// dependences between sets of memory access instructions. This is achieved
// through symbolic interval comparison. Regions are then versioned and the
// dynamic results of the interval tests are used to choose which version to
// execute.
//
// The following example:
//
//   for (int i = 0; i < N; i++) {
//     foo();
//     A[i] = B[i + M];
//  }
//
// Would become the following code:
//
//   // Tests if access to A and B do not overlap.
//   if ((A + N <= B) || (B + N + M <= A)) {
//     // Version of the loop with no depdendencies.
//     for (int i = 0; i < N; i++) {
//       foo();
//       A[i]{!alias-set:A} = B[i + M]{!alias-set:B};
//     }
//   } else {
//     // Version of the loop with unknown alias dependencies.
//     for (int i = 0; i < N; i++) {
//       foo();
//       A[i] = B[i + M];
//     }
//   }
//===----------------------------------------------------------------------===//

#ifndef ALIAS_INSTRUMENTER_H
#define ALIAS_INSTRUMENTER_H

#include "../PtrRangeAnalysis/PtrRangeAnalysis.h"

#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/IR/MDBuilder.h>
#include <functional>
#include <map>
#include <set>

using namespace llvm;

namespace lge {

class AliasInstrumentation : public FunctionPass {
  typedef IRBuilder<true, TargetFolder> BuilderType;
  typedef std::map<Value *, std::pair<Value *, Value *>> BoundMap;
  typedef std::set<std::pair<Value *, Value *>> ValuePairSet;

  // Analyses used.
  ScalarEvolution *SE;
  AliasAnalysis *AA;
  LoopInfo *LI;
  RegionInfo *RI;
  DominatorTree *DT;
  DominanceFrontier *DF;
  PtrRangeAnalysis *PtrRA;

  // Function being analysed.
  Function *CurrentFn;

  // Metadata domain to be used by alias metadata.
  MDNode *MDDomain = nullptr;

  std::set<BasicBlock*> ClonedBlocks;

  // [DBG]
  size_t ClonedLoops;

  // Walks the region tree, instrumenting the greatest possible regions.
  void instrumentRegion(Region *R);

  // Checks if there are basic properties that prevent us from instrumenting
  // this region, e.g., no exit block or absence of loops.
  bool canInstrument(Region *R);

  // Returns the single exiting block of the current function if it exists.
  // Returns NULL if more than one exit block is found.
  BasicBlock *getFnExitingBlock();

  // Generates dynamic checks that compare the access range of every pair of
  // pointers in the region at run-time, thus finding if there is true aliasing.
  // For every pair (A,B) of pointers in the region that may alias, we generate:
  // - check(A, B) -> upperAddrA + sizeOfA <= lowerAddrB ||
  //                  upperAddrB + sizeOfB <= lowerAddrA
  // The instructions needed for the checks compuation are inserted in the
  // entering block of the target region, which works as a pre-header. The
  // returned Instruction produces a boolean value that, at run-time, indicates
  // if the region is free of dependencies.
  Value *insertDynamicChecks(Region *R);

  // Create single entry and exit EDGES in a region (thus creating entering and
  // exiting blocks).
  void simplifyRegion(Region *R);

  // Group all predecessors that return true for HAS_PROPERTY into a single
  // predecessor block. If the content of the original block had to be moved to
  // another block, we pass the new block to CHANGE_LISTENER.
  void groupPredecessors(BasicBlock *BB,
                         std::function<bool(BasicBlock*)> hasProperty,
                         std::function<void(BasicBlock*)> changeListener);

  // Split the edge that connects SRC and DST, creating a new block. This was
  // taken from LLVM's "SplitCriticalEdge()". Updates dominator info.
  BasicBlock *splitEdge(BasicBlock *Src, BasicBlock *Dst);

  // Requests the insertion of the actual symbolic bounds expressions.
  void buildSCEVBounds(Region *R, SCEVRangeBuilder *RangeBuilder,
                       BoundMap *PointerBounds);

  // Determines which base pointers in the region need to be checked against
  // eachother. We only checks pointers for which we have range info.
  bool computePtrsDependence(Region *R, ValuePairSet *PtrPairsToCheck);

  // Inserts the actual interval comparison.
  Value *buildRangeCheck(Value *BasePtrA, Value *BasePtrB,
                         std::pair<Value *, Value *> BoundsA,
                         std::pair<Value *, Value *> BoundsB,
                         BuilderType *Builder, SCEVRangeBuilder *RangeBuilder);

  // Chain the checks that compare different pairs of pointers to a single
  // result value using "and" operations.
  // E.g.: %region-no-alias = %pair-no-alias1 && %pair-no-alias2 &&
  // %pair-no-alias3
  Value *chainChecks(std::vector<Value *> Checks, BuilderType *Builder);

  // Produce two versions of an instrumented region: one with the original
  // alias info, if the run-time alias check fails, and one set to ignore
  // dependencies between memory instructions, if the check passes.
  //     ____\|/___                 ____\|/___
  //    | dy_check |               | dy_check |
  //    '-----.----'               '-----.----'
  //     ____\|/___     =>      F .------'------. T
  //    | Region:  |         ____\|/__    _____\|/____
  //    |    ...   |        | (Alias) |  | (No alias) |
  //    '-----.----'        |    ...  |  |    ...     |
  //         \|/            '-----.---'  '------.-----'
  //                              '------.------'
  //                                    \|/
  void buildNoAliasClone(Region *R, Value *CheckResult);

  // Adds all blocks in a region to the set of cloned block.
  void registerClonedBlocks(Region *R);

  // Use scoped alias tags to tell the compiler that cloned regions are free of
  // dependencies. Basically creates a separate alias scope for each base
  // pointer in the region. Each load/store instruction is then associated with
  // it's base pointer scope, generating disjoint alias sets in the region.
  // Instructions for which we do not have range info or whose side-effects are
  // not known are not marked.
  void fixAliasInfo(Region *R);

public:
  static char ID;
  explicit AliasInstrumentation() : FunctionPass(ID) {}

  // FunctionPass interface.
  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  void releaseMemory()
  {
    ClonedBlocks.clear();
    ClonedLoops = 0;
  }
};

} // end lge namespace

namespace llvm {
class PassRegistry;
void initializeAliasInstrumentationPass(llvm::PassRegistry &);
}

namespace {
// Workaround to make the pass available from Clang. Initialize the pass as soon
// as the library is loaded.
class AIInitializer {
public:
  AIInitializer() {
    llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
    llvm::initializeAliasInstrumentationPass(Registry);
  }
};
static AIInitializer AIInit;
} // end of anonymous namespace.

#endif
