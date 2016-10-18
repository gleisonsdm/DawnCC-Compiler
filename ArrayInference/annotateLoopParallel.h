//===---------------------- annotateLoopParallel.h ----------------------===//
//
// This file is distributed under the Universidade Federal de Minas Gerais - 
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2015   Gleison Souza Diniz Mendon?a
//
//===----------------------------------------------------------------------===//
//
// Find a file named "out_pl.log" and try to insert metadata in all loop,
// when the file identify loops as parallel.
//
// To use this pass please use the flag "-annotateParallel", see the example
// available below:
//
// opt -load ${LIBR}/libLLVMArrayInference.so -annotateParallel ${BENCH}/$2.bc 
//
// The ambient variables and your signification:
//   -- LIBR => Set the location of ArrayInference tool location.
//   -- BENCH => Set the benchmark's paste.
// 
//===----------------------------------------------------------------------===//
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DebugInfo.h"

namespace llvm {

class Value;
class Instruction;
class LoopInfo;

class AnnotateParallel : public ModulePass {

  private:

  //===---------------------------------------------------------------------===
  //                              Data Structures
  //===---------------------------------------------------------------------===
  // Maps a function name to a list of lines in the source code that contain
  // parallel loops.
  std::map<std::string, std::vector<int> > Functions;

  // Maps a file name to a mapping between a function name suffix to a set
  // of indexes of loops in functions with that suffix which are parallel.
  // These indexes reflect the order in which the loops
  // appear in the original source code. For example, in the following function:
  // 1  void func(int **M) {
  // 2    for (int i = 0; i < 10; i++) {
  // 3      for (int j = 0; j < 10; j++) {
  // 4        M[i][j] = i*j;
  // 5      }
  // 6    }
  // 7    for (int i = 0; i < 10; i++) {
  // 8      M[i][i] = 0;
  // 9    }
  // 10 }
  // The loop in line 2 is has index 0. The one in line 3 is loop number 1.
  // The for loop in line 7 is the loop with index 2.
  std::map<std::string,
           std::map<std::string, std::set<int> > > ParallelLoopsIndexes;

  // Map from LLVM function to its corresponding DISubprogram.
  std::map<const Function *, const DISubprogram *> FunctionDebugInfo;
  
  // Container of module's debug info. Owns the data referenced by 
  // FunctionDebugInfo.
  DebugInfoFinder Finder;

  //===---------------------------------------------------------------------===

  // Find the lines to parallelize in standard input file.
  void readFile();

  // Read parallel loop annotations from a file passed by a command-line
  // argument.
  void readIndexesFile();

  // Populate FunctionDebugInfo.
  void readFunctionDebugInfo(Module &M);

  // Set Loop 'L' as parallel in the bytecode.
  void setMetadataParallelLoop(Loop *L);

  // This void calls regionIdentify for the top level region in function F.
  void functionIdentify(Function *F);

  public:

  static char ID;

  AnnotateParallel() : ModulePass(ID) {};
  
  // We need to insert the Instructions for each source file.
  virtual bool runOnModule(Module &M) override;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequiredTransitive<LoopInfoWrapperPass>();
      AU.setPreservesAll();
  }

  LoopInfo *li;
};

}

//===------------------------ annotateLoopParallel.h ----------------------===//
