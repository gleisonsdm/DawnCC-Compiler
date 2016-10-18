//===----------------------- regionReconstructor.cpp ----------------------===//
//
// This file is distributed under the Universidade Federal de Minas Gerais - 
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2015   Gleison Souza Diniz Mendon?a
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
#include "llvm/IR/DIBuilder.h" 
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/Statistic.h"

#include "regionReconstructor.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "RegionReconstructor"

void RegionReconstructor::insertReducedRegion (Region *R, Region *Rr) {
  if (reducedRegion.count(R) != 0)
    return;
  reducedRegion[R] = Rr;
}

Region* RegionReconstructor::returnReducedRegion (Region *R) {
  if (!reducedRegion[R]) 
    return nullptr;
  return reducedRegion[R];
}

bool RegionReconstructor::isTriviallySafetly (Region *R) {
  for (auto BB = R->block_begin(), BE = R->block_end(); BB != BE; BB++) {
    for (auto I = (*BB)->begin(), IE = (*BB)->end(); I != IE; I++) {
      if ((isa<LoadInst>(I) || isa<StoreInst>(I)) && !li->getLoopFor(*BB)) {
        return false;
      }
    }
  }
  return true;
}

bool RegionReconstructor::isSafetly (Region *R) {
  // TO - DO : use Pericles algorithm here.
  return isTriviallySafetly(R);
}

void RegionReconstructor::analyzeRegion (Region *R) {
  // Identyfy the regions recursively.
  for (auto SR = R->begin(), SRE = R->end(); SR != SRE; ++SR)
    analyzeRegion(&(**SR));

  if (++R->block_begin() == R->block_end()) {
    // Generate a stub to invalid sub region.
    insertReducedRegion(R, nullptr);
    return;
  }

  DominatorTree *DT = dt;
  RegionInfo *RI = &rp->getRegionInfo();
  BasicBlock *BB = *(++R->block_begin());
  BasicBlock *BE = nullptr;
  Region *subR = nullptr;
  
  // Obtain the max region, case exists. Returns nullptr in the other case.
  if (BB)
    BE = RI->getMaxRegionExit(BB);

  if (!BB || !BE) {
    // Generate a stub to invalid sub region.
    insertReducedRegion(R, nullptr);
    return;
  }

  // Generate a new subRegion, using RegionInfo's information.
  if (BE)
    subR = new Region(BB, BE, RI, DT, R); 
  
  if (!subR) {
    // Generate a stub to invalid sub region.
    insertReducedRegion(R, nullptr);
    return;
  }

  insertReducedRegion(R, subR);
}

void RegionReconstructor::analyzeFunction (Function *F) {
  // Indetify the top region.
  Region *region = rp->getRegionInfo().getRegionFor(F->begin()); 
  Region *topRegion = region;
  while (region != NULL) {
    topRegion = region;
    region = region->getParent();
  }
     
  // Try analyzes top region.
  analyzeRegion(topRegion);
}

bool RegionReconstructor::runOnFunction(Function &F) {
  this->li = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  this->rp = &getAnalysis<RegionInfoPass>();
  this->aa = &getAnalysis<AliasAnalysis>();
  this->se = &getAnalysis<ScalarEvolution>();
  this->dt = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  // In this step, the "functionIdentify" find the top level loop
  // to apply our techinic.
  analyzeFunction(&F);

  return true;
}

char RegionReconstructor::ID = 0;
static RegisterPass<RegionReconstructor> Z("region-Reconstructor",
"Generate sub regions to LLVM IR.");

//===----------------------- regionReconstructor.cpp ----------------------===//
