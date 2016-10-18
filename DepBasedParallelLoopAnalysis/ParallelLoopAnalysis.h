// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]
//
// This pass finds loops whose iterations can be performed completely in
// parallel. To do so, it uses LLVM's dependence analysis. Notice that there can
// be cases where iterations of a given loop can be performed in parallel, but
// iterations of one or more of its inner loops cannot. Example:
//
//   for (int i = 0; i < N; ++i)
//     for (int j = 1; j < M; ++j)
//       a[i][j] += a[i][j-1];

#ifndef PARALLEL_LOOP_ANALYSIS_H
#define PARALLEL_LOOP_ANALYSIS_H

#include <llvm/Analysis/DependenceAnalysis.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <set>

namespace llvm {
class Loop;
}

namespace lge {

class ParallelLoopAnalysis : public llvm::FunctionPass {
  // Analyses used.
  llvm::DependenceAnalysis *DA;
  llvm::LoopInfo *LI;
  llvm::ScalarEvolution *SE;
  std::set<const llvm::Loop*> CantParallelize;

  // Registers a dependence between two instructions.
  void inspectMemoryDependence(llvm::Dependence &D, llvm::Instruction &Src,
    llvm::Instruction &Dst);
  void checkRegisterDependencies(llvm::Loop *);

public:
  static char ID;
  explicit ParallelLoopAnalysis() : FunctionPass(ID) {}

  // FunctionPass interface.
  virtual bool runOnFunction(llvm::Function &F);
  virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const;
  void releaseMemory() { CantParallelize.clear(); }

  bool canParallelize(llvm::Loop* L);
};

} // end lge namespace

namespace llvm {
class PassRegistry;
void initializeParallelLoopAnalysisPass(llvm::PassRegistry &);
}

namespace {
// Workaround to make the pass available from Clang. Initialize the pass as soon
// as the library is loaded.
class ParLoopInitializer {
public:
  ParLoopInitializer() {
    llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
    llvm::initializeParallelLoopAnalysisPass(Registry);
  }
};
static ParLoopInitializer ParLoopInit;
} // end of anonymous namespace.

#endif
