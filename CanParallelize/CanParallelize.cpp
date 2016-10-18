// Author: Kezia Andrade [kezia.andrade@dcc.ufmg.br]

#include "CanParallelize.h"

#include <llvm/IR/Metadata.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LegacyPassManager.h>
#include "llvm/IR/DIBuilder.h" 
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <fstream>
#include <vector>

using namespace llvm;
using namespace lge;

void CanParallelize::visit(Loop *L) {  
  LoopCounter++;
 
  if (ParLoops->canParallelize(L)){
    OutFile << std::to_string(L->getStartLoc().getLine()) << ";";
    Parallel=true;
  }

  const std::vector<Loop *> &subLoops = L->getSubLoops();

  for (auto SL : subLoops)
    visit(SL);
}

bool CanParallelize::runOnFunction(llvm::Function &F) {
  ParLoops = &getAnalysis<ParallelLoopAnalysis>();
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  
  LoopCounter=0;
  Parallel=0;
  
  if(FirstFunction){
    OutFile.open("out_pl.log", std::ios_base::out);
    FirstFunction=false;
  }
  else{
    OutFile.open("out_pl.log", std::ios_base::app);
  }
  
  std::string name = F.getName();
  OutFile << name <<";";

  for (auto I = LI->begin(), E = LI->end(); I != E; ++I) {
    visit(*I);
  }
  if (!Parallel)
    OutFile << "-1;";
  
  OutFile << "\n";
  OutFile.close();

  return false;
}

void CanParallelize::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<ParallelLoopAnalysis>();
  AU.addRequired<LoopInfoWrapperPass>();

  AU.setPreservesAll();
}

static cl::opt<bool> RunCanParallelize(
    "can-par",
    cl::desc("Map which Loop is Parallel"),
    cl::init(false), cl::ZeroOrMore);

char CanParallelize::ID = 0;

static void registerCanParallelize(const PassManagerBuilder &Builder,
                                            legacy::PassManagerBase &PM) {
  if (!RunCanParallelize)
    return;

  PM.add(new CanParallelize());
}

static RegisterStandardPasses
  RegisterCanParallelize(PassManagerBuilder::EP_EarlyAsPossible,
                                  registerCanParallelize);

INITIALIZE_PASS_BEGIN(CanParallelize, "can-parallelize",
                      "Map which Loop is Parallel", true, true);
INITIALIZE_PASS_DEPENDENCY(ParallelLoopAnalysis);
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass);
INITIALIZE_PASS_END(CanParallelize, "can-parallelize",
                    "Map which Loop is Parallel", true, true)
