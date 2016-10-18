//===--------------------------- Coalescing.h ----------------------------===//
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
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"

#include "../PtrRangeAnalysis.h"

#define TO 1
#define FROM 2
#define TOFROM 3

using namespace lge;

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

class Coalescing : public FunctionPass {

  private:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
  
  typedef struct SymbolicMemoryRegion {
    
    // Base Pointer to generate a symbolic Memory Region.
    Value *basePointer;

    // Store the access functions to this pointer.
    std::vector<const SCEV *> accessFunctions;
    
    // Upper and Lower Bounds.
    Value *lowerBound;
    Value *upperBound;
    
    // Associate to the region the follow memory access model:
    //  TO -> Load Instructions can read data in the device.
    //  FROM -> Store Instructions can write data in the device.
    //  TOFROM -> When the pointer is associate with Loads and Stores in the
    //            same region.
    char mappingType;

  } SymbolicMemoryRegion;
 
  typedef std::vector<SymbolicMemoryRegion> AccessPointers;  

  // A mappingLoop associate information to use the analysis.
  typedef struct MappingLoop {
    AccessPointers pointers;
    bool isLoopParallel;
    // Store the small region that contains the loop.
    Region *region;
  } MappingLoop;

  std::map<Loop*, MappingLoop> nestMappings;
  /// Map<LoopNest, Set<Pair<SymbolicMemoryRegion, MappingType>>> nestMappings =
  /// computeLoopNestMappings(f, parallelLoopNests, rangeInfo) 
  
  std::map<Region*, AccessPointers> regionMappings;

  //===---------------------------------------------------------------------===

  // Copy access functions present in "inAFunc" to "outAFunc".
  void copyAccessFunctions (std::vector<const SCEV *> & inAFunc,
                            std::vector<const SCEV *> & outAFunc);

  // Use of Loop Parallel Analysis to identify parallel loops.
  bool isLoopParallel (Loop *L);

  // Insert in an AccessPointers list a new pointer:
  void insertAccessPointer (AccessPointers & ptrs, SymbolicMemoryRegion ptr);
 
  // Update V to the basePointer of V.
  void getBasePtr (Value *V);

  // Generate information to the loop L, if possible.
  void computeLoopNestMappings (Loop *L, AccessPointers pointers, Region *R);
  
  // Generate information to the Region R, if possible.
  void computeRegionNestMappings (Region *R);

  // Uses a subregion tree for Region R to identify regions.
  void regionIdentify (Region *R);
  
  // Provides Loop and Region information to make several coallescing.
  void computeMappings (Function *F);

  public:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
  
  //===---------------------------------------------------------------------===

  static char ID;

  Coalescing() : FunctionPass(ID) {};
  
  // We need to insert the Instructions for each source file.
  virtual bool runOnFunction(Function &F) override;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<RegionInfoPass>();
      AU.addRequired<AliasAnalysis>();
      AU.addRequired<ScalarEvolution>();
      AU.addRequiredTransitive<LoopInfoWrapperPass>();
      AU.addRequired<PtrRangeAnalysis>();
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.setPreservesAll();
  }

  PtrRangeAnalysis *ptrRA;
  RegionInfoPass *rp;
  AliasAnalysis *aa;
  ScalarEvolution *se;
  LoopInfo *li;
  DominatorTree *dt;

};

}

//===--------------------------- Coalescing.h ----------------------------===//
