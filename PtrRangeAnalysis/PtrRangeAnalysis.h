// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]
//
// This analysis aims to determine if symbolic bounds can be computed for the
// pointers in a program. The pass traverses the region tree and, for each
// region, checks if the base pointers of all memory instructions have bounds
// that can be placed right at the region entry.
//
// The main goal of this pass is to populate the "RegionsRangeData" map. This
// map contains, for each region in the function, a list of base pointers for
// which range data is known and if all memory side-effects in the region can be
// determined ("HasFullSideEffectInfo" flag). For each base pointer, it also
// stores the list of access expresions for which bounds can be computed.
//
// After this analysis runs, the user can pass the extracted data to the
// SCEVRangeBuilder utility, to insert instructions to compute the actual
// symbolic bounds at the region entry. A small example of how this can be
// achieved is as follows:
//
//    Region *r = ... // get a region somehow.
//    PtrRangeAnalysis ptrRA = &getAnalysis<PtrRangeAnalysis>();
//
//    // Fail if there may be instructions with unknown side-effects.
//    if (!ptrRA->RegionsRangeData[r].HasFullSideEffectInfo)
//      return;
//
//    // Insert bounds computation right at the region entry.
//    Instruction *insertPt = r->getEntry()->getFirstNonPHI();;
//    SCEVRangeBuilder rangeBuilder(se, aa, li, dt, r, insertPt);
//
//    // Generate and store both bounds for each base pointer in the region.
//    std::map<Value *, std::pair<Value *, Value *> > pointerBounds;
//    for (auto& pair : ptrRA->RegionsRangeData[r].basePtrsData) {
//      Value *low = rangeBuilder.getULowerBound(pair.second.accessFunctions);
//      Value *up = rangeBuilder.getUUpperBound(pair.second.accessFunctions);
//
//      // Adds "sizeof(element)" to the upper bound of a pointer, so it gives
//      // us the address of the first byte after the memory region.
//      up = rangeBuilder.stretchPtrUpperBound(pair.first, up);
//
//      pointerBounds.insert(std::make_pair(pair.first,
//                                          std::make_pair(low, up)));
//    }

#ifndef PTR_RANGE_ANALYSIS_H
#define PTR_RANGE_ANALYSIS_H

#include "SCEVRangeBuilder.h"

#include <llvm/Analysis/RegionInfo.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <map>

using namespace llvm;

namespace lge {

class PtrRangeAnalysis : public FunctionPass {
  /**
   * Holds range data for the memory operations in a region.
   */
  struct RegionRangeInfo {
    /**
     * Symbolic range info for a single base pointer.
     */
    struct PtrRangeInfo {
      Value *BasePtr;

      // List of instructions known to access this base pointer and their
      // respective symbolic access expressions.
      std::vector<Instruction *> AccessInstructions;
      std::vector<const SCEV *> AccessFunctions;

      PtrRangeInfo() {}
      PtrRangeInfo(Value *V) : BasePtr(V) {}
    };

    Region *R;

    // This field indicates that the memory side-effects of every instruction
    // within the region are known. That means:
    // - the region has no function calls or, if it does, they don't
    //   manipulate memory.
    // - there are no instructions whose base pointer or access function are
    //   not known.
    // - Symbolic ranges of all base pointers in the region are computable.
    bool HasFullSideEffectInfo;

    // Range data for each base pointer in the region. For the accesses a[i],
    // a[i+5], and b[i+j], we'd have something like:
    // - basePtrsInfo: {a: (i,i+5), b: (i+j)}
    std::map<Value *, PtrRangeInfo> BasePtrsData;

    RegionRangeInfo() : HasFullSideEffectInfo(false) {}
    RegionRangeInfo(Region *R) : R(R), HasFullSideEffectInfo(false) {}
  };

  // Analyses used.
  ScalarEvolution *SE;
  AliasAnalysis *AA;
  LoopInfo *LI;
  RegionInfo *RI;
  DominatorTree *DT;

  // Function being analysed.
  Function *CurrentFn;

  // Collects range data for a single instruction. Returns false if the
  // instruction can have memory side-effects but we were not able to extract
  // range information for it.
  bool collectRangeInfo(Instruction *Inst, RegionRangeInfo *RegionData,
                        SCEVRangeBuilder *RangeBuilder);

  // Collects range data for a whole region.
  void collectRangeInfo(Region *R);

public:
  static char ID;
  explicit PtrRangeAnalysis() : FunctionPass(ID) {}

  // Set of regions in the function and their respective range data.
  std::map<Region *, RegionRangeInfo> RegionsRangeData;

  // FunctionPass interface.
  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  void releaseMemory() { RegionsRangeData.clear(); }
};

// Get the value that represents the base pointer of the given memory
// instruction in the given region. The pointer must be region invariant.
Value *getBasePtrValue(Instruction *Inst, const Region *R, LoopInfo *LI,
                       AliasAnalysis *AA, ScalarEvolution *SE);

// Determines if the elements referenced by a pointer have known offset size
// in memory. This will return false for things like function pointers.
bool hasKnownElementSize(Value *BasePtr);

// [Utility taken from Polly] Returns the value representing the target address
// of a memory operation or a pointer arithmetic expression (GEP).
Value *getPointerOperand(Instruction *Inst);

// [Utility taken from Polly] Checks if a given value is invariant within a
// region, i.e, the value is a region input.
bool isInvariant(const Value *Val, const Region *Reg, LoopInfo *LI,
                 AliasAnalysis *AA);

} // end lge namespace

namespace llvm {
class PassRegistry;
void initializePtrRangeAnalysisPass(llvm::PassRegistry &);
}

namespace {
// Workaround to make the pass available from Clang. Initialize the pass as soon
// as the library is loaded.
class PtrRAStaticInitializer {
public:
  PtrRAStaticInitializer() {
    llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
    llvm::initializePtrRangeAnalysisPass(Registry);
  }
};
static PtrRAStaticInitializer PtrRAInit;
} // end of anonymous namespace.

#endif
