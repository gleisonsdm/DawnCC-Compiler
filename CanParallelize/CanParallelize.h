//===------------------------ Parallelize.h --------------------------===//
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
// This pass identify what loop can be parallelized and inserts into a 
// file which loop can parallelize. To do so, it uses ParallelLoopAnalysis.
//
//===--------------------------------------------------------------------------===//

#ifndef CAN_PARALLELIZE_H
#define CAN_PARALLELIZE_H

#include "../DepBasedParallelLoopAnalysis/ParallelLoopAnalysis.h"

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Metadata.h>
#include <fstream>

using namespace llvm;

namespace lge {

class CanParallelize : public FunctionPass {
  // Analyses used.
  ParallelLoopAnalysis *ParLoops;
  LoopInfo *LI;
  size_t LoopCounter;
  bool FirstFunction=true;
  bool Parallel=false;
  std::ofstream OutFile;
  //OutFile.open("/tmp/out_pl.log", std::ios_base::out);
  //OutFile << "function;how many loops;parallelLoop1;parallelLoop2;end;\n";
  //OutFile.close();  

  void visit(Loop *L);

public:
  static char ID;
  explicit CanParallelize() : FunctionPass(ID) {}

  // FunctionPass interface.
  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  void releaseMemory() {}
};

} // end lge namespace

namespace llvm {
class PassRegistry;
void initializeCanParallelizePass(llvm::PassRegistry &);
}

namespace {
// Initialize the pass as soon as the library is loaded.
class CanParInitializer {
public:
  CanParInitializer() {
    llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
    llvm::initializeCanParallelizePass(Registry);
  }
};
static CanParInitializer CanParInit;
} // end of anonymous namespace.

#endif
