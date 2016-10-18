//===------------------------ ParallelLoopMetadata.h --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the Universidade Federal de Minas Gerais -
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2015 Kezia Andrade
//
//===--------------------------------------------------------------------------===//
//
// This pass identify what loop can be parallelized and inserts into the 
// source code a metadata which indicates the loop is parallel. To do so, 
// it uses ParallelLoopAnalysis.
//
//===--------------------------------------------------------------------------===//

#ifndef PARALLEL_LOOP_METADATA_H
#define PARALLEL_LOOP_METADATA_H

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Metadata.h>
#include <vector>

using namespace llvm;

namespace lge {

class ParallelLoopMetadata : public FunctionPass {
  // Analyses used.
  LoopInfo *LI;
  size_t LoopCounter;
  size_t CountPar;
  size_t CountDiv;
  int FunctionCounter=0;
  std::vector <std::string> Functions;
  std::vector <std::vector <int>> LoopsFunction;
  std::vector <std::vector <int>> DivLoops;

  void visit(Loop *L,bool Par,bool Div);

  void readFile(bool ParAnalysis, std::ifstream &InFile);
  void setMetadataParallelLoop (Loop *L, bool ParAnalysis);

public:
  static char ID;
  explicit ParallelLoopMetadata() : FunctionPass(ID) {}

  // FunctionPass interface.
  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  void releaseMemory() {}
};

} // end lge namespace

namespace llvm {
class PassRegistry;
void initializeParallelLoopMetadataPass(llvm::PassRegistry &);
}

namespace {
// Initialize the pass as soon as the library is loaded.
class ParLoopMetadataInitializer {
public:
  ParLoopMetadataInitializer() {
    llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
    llvm::initializeParallelLoopMetadataPass(Registry);
  }
};
static ParLoopMetadataInitializer ParLoopMetadataInit;
} // end of anonymous namespace.

#endif
