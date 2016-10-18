// Author: Kezia Andrade [kezia.andrade@dcc.ufmg.br]

#include "ParallelLoopMetadata.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <fstream>
#include <vector>

using namespace llvm;
using namespace lge;

void ParallelLoopMetadata::setMetadataParallelLoop (Loop *L, bool ParAnalysis) {
  BasicBlock *BB = L->getHeader();
  if (BB == nullptr)
    return;
  LLVMContext& C = BB->getTerminator()->getContext();
  if (ParAnalysis) {
    MDNode* N = MDNode::get(C, MDString::get(C, "Parallel Loop Metadata"));
    BB->getTerminator()->setMetadata("isParallel", N);
  }
  else {
    MDNode* N = MDNode::get(C, MDString::get(C, "Divergent Loop Metadata"));
    BB->getTerminator()->setMetadata("isDivergent", N);
  }
}

void ParallelLoopMetadata::visit(Loop *L, bool Par, bool Div) {
  LoopCounter++;
  
  if (Par) {
    int LoopPar = LoopsFunction[FunctionCounter][(CountPar - 1)];
    if(LoopCounter == LoopPar) {
      setMetadataParallelLoop(L,1);
      CountPar++;
    }
  }
  if (Div){
    int LoopDiv = DivLoops[FunctionCounter][CountDiv];
  
    if(LoopCounter == LoopDiv) {
      setMetadataParallelLoop(L,0);
      CountDiv++;
    }
  }
  const std::vector<Loop *> &subLoops = L->getSubLoops();

  for (auto SL : subLoops)
    visit(SL,Par,Div);
}

void ParallelLoopMetadata::readFile(bool ParAnalysis, std::ifstream &InFile) {
 
  std::string Line;
  int i=0;
  while (std::getline(InFile, Line)) {
    char * Cstr = new char [Line.length()+1];
    std::strcpy (Cstr, Line.c_str());
    
    char * Words = std::strtok (Cstr,";");
    int j=0; 
    std::vector <int> NumLoop;
    while (Words != NULL) {
      if (!j) 
        Functions.push_back(Words);
      else
        NumLoop.push_back(atoi(Words));
      
      Words = std::strtok (NULL, ";");
      j++;      
    }
    if (ParAnalysis)
      LoopsFunction.push_back(NumLoop);
    else
      DivLoops.push_back(NumLoop);
    i++;
  }
}  

bool ParallelLoopMetadata::runOnFunction(llvm::Function &F) {
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  LoopCounter = 0;
  CountPar = 0,CountDiv = 0;
  bool Par = 0, Div = 0; 
  
  if (!FunctionCounter) {
    std::ifstream PlFile, DaFile;
    
    PlFile.open("out_pl.log", std::ios_base::in);
    if (PlFile.is_open()) {
      readFile(1, PlFile);
      Par = 1;
    } else
      Par = 0;
    PlFile.close();
    
    DaFile.open("out_da.log", std::ios_base::in);
    if (DaFile.is_open()) {
      readFile(0, DaFile);
      Div = 1;
    } else
      Div = 0;
    DaFile.close();
  }

  if (F.getName() == Functions[FunctionCounter]) {
    
    if (Par) {    
      if (LoopsFunction[FunctionCounter][CountPar] != 0) 
        CountPar++;
      else 
        Par = 0;
    }
    if (Div) {
      if(DivLoops[FunctionCounter][CountDiv] != 0)  
        CountDiv++;
      else
        Div = 0;
    }
    if(Par || Div){   
      for (auto I = LI->begin(), E = LI->end(); I != E; ++I) {
        visit(*I,Par,Div);
      }
    }
  }
  
  FunctionCounter++;
  return false;
}

void ParallelLoopMetadata::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();

  AU.setPreservesAll();
}

static cl::opt<bool> RunParallelLoopMetadata(
    "parloops-md",
    cl::desc("Mark Loop as Parallel"),
    cl::init(false), cl::ZeroOrMore);

char ParallelLoopMetadata::ID = 0;

static void registerParallelLoopMetadata(const PassManagerBuilder &Builder,
                                            legacy::PassManagerBase &PM) {
  if (!RunParallelLoopMetadata)
    return;

  PM.add(new ParallelLoopMetadata());
}

static RegisterStandardPasses
  RegisterParallelLoopMetadata(PassManagerBuilder::EP_EarlyAsPossible,
                                  registerParallelLoopMetadata);

INITIALIZE_PASS_BEGIN(ParallelLoopMetadata, "parallel-loop-metadata",
                      "Mark Loop as Parallel", true, true);
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass);
INITIALIZE_PASS_END(ParallelLoopMetadata, "parallel-loop-metadata",
                    "Mark Loop as Parallel", true, true)
