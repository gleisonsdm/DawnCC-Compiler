//===----------------------- regionReconstructor.h ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the Universidade Federal de Minas Gerais -
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2016   Gleison Souza Diniz Mendon?a
//
//===----------------------------------------------------------------------===//
//
//  Trying to improve DawnCC's memory coalescing, are proposed a new desing
//  to explore the program regions, trying to re-build the LLVM's regions to
//  analyze it, reducing alias effects to analyze with PtrRangeAnalysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/DominanceFrontier.h"

namespace llvm {
class ScalarEvolution;
class AliasAnalysis;
class SCEV;
class DominatorTree;

class DominanceFrontier;
struct PostDominatorTree;
class Value;
class Region;
class Instruction;
class LoopInfo;
class ArrayInference;

class RegionReconstructor : public FunctionPass {

  private:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
  
  // For each region, Store the correspondent region with alias reduction.
  // In pratice, generates a new sub region to analyze.
  std::map<Region*, Region*> reducedRegion;

  //===---------------------------------------------------------------------===
  
  // Store the reduced region (Rr), assosiating it with the region R.
  void insertReducedRegion (Region *R, Region *Rr);

  // Analyze region R and return true case R is safetly.
  bool isTriviallySafetly (Region *R);
 
  // Analyze region R, trying to generate a valid "reduced region".
  // This region is exactly a new region, with all basic blocks less the first
  // BasicBlock of the original region.
  void analyzeRegion (Region *R);

  // Analyze the function F, trying to identify and reduce all sub regions.
  void analyzeFunction (Function *F);

  public:

  // Return the region reduced of the region R.
  Region* returnReducedRegion (Region *R);

  static char ID;

  RegionReconstructor () : FunctionPass(ID) {};
  
  // We need to insert the Instructions for each source file.
  virtual bool runOnFunction(Function &F) override;
  
// Analyze region R and identify case memory coalescing is completly safetly.
  bool isSafetly (Region *R);

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<RegionInfoPass>();
      AU.addRequired<AliasAnalysis>();
      AU.addRequired<ScalarEvolution>();
      AU.addRequiredTransitive<LoopInfoWrapperPass>();
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.setPreservesAll();
  }

  RegionInfoPass *rp;
  AliasAnalysis *aa;
  ScalarEvolution *se;
  LoopInfo *li;
  DominatorTree *dt;

};

}

//===---------------------- regionReconstructor.h -------------------------===//
