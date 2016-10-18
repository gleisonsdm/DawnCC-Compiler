//===--------------------------- Coalescing.cpp --------------------------===//
//
// This file is distributed under the Universidade Federal de Minas Gerais - 
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2015   Gleison Souza Diniz Mendon?a
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include <fstream>

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

#include "Coalescing.h"

using namespace llvm;
using namespace std;
using namespace lge;

#define DEBUG_TYPE "Coalescing"

void Coalescing::copyAccessFunctions (std::vector<const SCEV *> & inAFunc,
                                       std::vector<const SCEV *> & outAFunc) {
  for (auto I = inAFunc.begin(), IE = inAFunc.end(); I != IE; I++) {
    outAFunc.push_back(*I);
  }
}

bool Coalescing::isLoopParallel (Loop *L) {
  BasicBlock *BB = L->getLoopLatch();
  MDNode *MD = nullptr;
  MDNode *MDDivergent = nullptr;
  if (BB == nullptr)
    return false;
  MD = BB->getTerminator()->getMetadata("isParallel");
  if (!MD)
    return false;
  return true;
}

void Coalescing::insertAccessPointer (AccessPointers & ptrs,
                                       SymbolicMemoryRegion ptr) {
  for(auto I = ptrs.begin(), IE = ptrs.end(); I != IE; I++) {
    if (I->basePointer == ptr.basePointer)
      return; 
  }
  ptrs.push_back(ptr);
}

void Coalescing::getBasePtr(Value *V) {
 while(true) { 
   if (LoadInst *LI = dyn_cast<LoadInst>(V)) 
     V = LI->getPointerOperand(); 
   else if (StoreInst *ST = dyn_cast<StoreInst>(V)) 
     V = ST->getPointerOperand();
   else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(V))
     V = GEP->getPointerOperand();
   else
     break;
 }
}

void Coalescing::computeLoopNestMappings (Loop *L, AccessPointers pointers,
Region *R) {
  MappingLoop mapLoop;
  mapLoop.pointers = pointers;
  mapLoop.isLoopParallel = isLoopParallel(L);
  mapLoop.region = R;
  if (!nestMappings.count(L))
    nestMappings[L] = mapLoop;
}

void Coalescing::computeRegionNestMappings (Region *R) {
  // Indetify pointers for region R.
  AccessPointers ptrs;
  Module *M = R->block_begin()->getParent()->getParent();
  const DataLayout DT = DataLayout(M);
  Instruction *insertPt = R->getEntry()->getFirstNonPHI();
  SCEVRangeBuilder rangeBuilder(se, DT, aa, li, dt, R, insertPt);

  for (auto& pair : ptrRA->RegionsRangeData[R].BasePtrsData) {
    // Adds "sizeof(element)" to the upper bound of a pointer, so it gives us
    // the address of the first byte after the memory region.
    Value *low = rangeBuilder.getULowerBound(pair.second.AccessFunctions);
    Value *up = rangeBuilder.getUUpperBound(pair.second.AccessFunctions);
    up = rangeBuilder.stretchPtrUpperBound(pair.first, up);
    SymbolicMemoryRegion memreg;
    
    // Provide information to run the analysis.
    memreg.basePointer = pair.first;
    memreg.lowerBound = low;
    memreg.upperBound = up;
    memreg.mappingType = ptrRA->getPointerAcessType(R, pair.first);
    copyAccessFunctions(pair.second.AccessFunctions, memreg.accessFunctions);
    insertAccessPointer(ptrs, memreg);
  }  

  Loop *L = li->getLoopFor(*(R->block_begin()));
  if (L)
    computeLoopNestMappings(L, ptrs, R);
  
  if (!regionMappings.count(R))
    regionMappings[R] = ptrs;
}

void Coalescing::regionIdentify (Region *R) {
  computeRegionNestMappings(R);
  // Repeat processament for each sub region.
  for (auto SR = R->begin(), SRE = R->end(); SR != SRE; ++SR)
    regionIdentify(&(**SR));
}

void Coalescing::computeMappings (Function *F) {
  // Indetify the top region.
  Region *region = rp->getRegionInfo().getRegionFor(F->begin()); 
  Region *topRegion = region;
  while (region != NULL) {
    topRegion = region;
    region = region->getParent();
  }
     
  regionIdentify(topRegion);
}

bool Coalescing::runOnFunction(Function &F) {
  this->li = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  this->rp = &getAnalysis<RegionInfoPass>();
  this->aa = &getAnalysis<AliasAnalysis>();
  this->se = &getAnalysis<ScalarEvolution>();
  this->dt = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  this->ptrRA = &getAnalysis<PtrRangeAnalysis>();
  
  computeMappings(&F);

  return true;
}

char Coalescing::ID = 0;
static RegisterPass<Coalescing> Z("coalescing",
"Memory coalescing algorithm.");

//===--------------------------- Coalescing.cpp --------------------------===//
