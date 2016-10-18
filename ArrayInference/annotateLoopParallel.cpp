//===---------------------- annotateLoopParallel.cpp ----------------------===//
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

#include <fstream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <set>

#include "llvm/Analysis/RegionInfo.h"  
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DIBuilder.h" 
#include "llvm/IR/DebugInfoMetadata.h" 
#include "llvm/IR/LLVMContext.h" 
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/Statistic.h"

#include "annotateLoopParallel.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "annotateParallel"

void AnnotateParallel::setMetadataParallelLoop (Loop *L) {
  // Mark loop 'L' as parallel using metadata.
  BasicBlock *BB = L->getHeader();
  if (BB == nullptr)
    return;
  LLVMContext& C = BB->getTerminator()->getContext();
  MDNode* N = MDNode::get(C, MDString::get(C, "Parallel Loop Metadata"));
  BB->getTerminator()->setMetadata("isParallel", N);
}

void AnnotateParallel::readFile() {   
  // Read file "out_pl" and try to infer parallel loops.
  std::ifstream InFile;
  InFile.open("out_pl.log", std::ios_base::in);
  if (!InFile.is_open())
    return;
  
  std::string Line = std::string(), name = std::string();
  while (std::getline(InFile, Line)) {
    name = std::string();
    unsigned int i = 0, ie = Line.length();
    for (;(i != ie) && (Line[i] != ';'); i++) 
      name += Line[i];
    i++;
    if (Line[i] == '-' && Line[(i+1)] == '1') {
      continue;
    }
    while (i != ie) {
      int tmp  = 0;
      for (;(i!=ie) && (Line[i] != ';'); i++) {
        tmp = (tmp * 10);
        tmp += (Line[i] - '0');
      }
      Functions[name].push_back(tmp);
      i++;
    }
  }
  InFile.close();
}

static cl::opt<std::string> ParallelLoopAnnotations(
    "parallel-loop-indexes",
    cl::desc("Path to file containing the indexes of parallel loop in each "
             "function."));

void AnnotateParallel::readIndexesFile() {
  if (!ParallelLoopAnnotations.size())
    return;

  std::ifstream AnnotationsFile(ParallelLoopAnnotations);
  if (!AnnotationsFile)
    return;

  std::string Line;

  while (std::getline(AnnotationsFile, Line)) {
    std::istringstream Stream(Line);
    std::string FileName, FunctionNameSuffix, IndexesList;

    if (Stream >> FileName >> FunctionNameSuffix >> IndexesList) {
      // IndexesList is a list of integers separated by commas. For each
      // index in it, we insert an entry into ParallelLoopsIndexes.

      // Index of the first character of the next loop index to be inserted.
      unsigned Begin = 0; 
      for (unsigned I = 0, Size = IndexesList.size(); I <= Size; ++I) {
        if (I == Size || IndexesList[I] == ',') {
          if (Begin < I) {
            ParallelLoopsIndexes[FileName][FunctionNameSuffix].insert(
                std::atoi(IndexesList.substr(Begin, I - Begin).c_str()));
          }
          Begin = I+1;
        }
      }
    }
  }
}

namespace {
// Given a file path (like /a/b/c), returns the file name (e.g. c).
StringRef FileNameFromPath(StringRef FileName) {
  // Remove trailing slashes.
  while (FileName.size() && FileName[FileName.size() - 1] == '/') {
    FileName = FileName.drop_back();
  }
  size_t SlashIndex = FileName.find_last_of('/');
  if (SlashIndex == StringRef::npos)
    return FileName;
  return FileName.drop_front(SlashIndex + 1);
}

} // namespace

void AnnotateParallel::functionIdentify (Function *F) {
  // Write annotations from the output of CanParallelize.
  if (Functions.count(F->getName())) {
    std::map<Loop*, bool> Loops;
    // Identify and insert metadata on each loop available, case parallel.
    for (auto B = F->begin(), BE = F->end(); B != BE; B++) {  
      Loop *l = li->getLoopFor(B);
      if (l && Loops.count(l) == 0) {
        // Set loop as parallel.
        for (int i = 0, ie = Functions[F->getName()].size(); i != ie; i++)
          if (Functions[F->getName()][i] == l->getStartLoc()->getLine())
            setMetadataParallelLoop(l);
        Loops[l] = true;
      }
    }
  }

  // Write annotations from the file passed by command-line argument.
  const std::set<int> *ParallelLoops = nullptr;
  auto Subprogram = FunctionDebugInfo[F];
  std::string FileName = (Subprogram && Subprogram->getFile())
                         ? FileNameFromPath(Subprogram->getFile()->getFilename())
                         : StringRef();
  auto LoopsInFileIt = ParallelLoopsIndexes.find(FileName);

  if (LoopsInFileIt != ParallelLoopsIndexes.end()) {
    const auto &LoopsInFile = LoopsInFileIt->second;
    for (auto LoopsInFunctionIt = LoopsInFile.begin(), End = LoopsInFile.end();
         LoopsInFunctionIt != End; ++LoopsInFunctionIt) {
      if (F->getName().endswith(StringRef(LoopsInFunctionIt->first))) {
        ParallelLoops = &LoopsInFunctionIt->second;
        break;
      }
    }
  }

  if (ParallelLoops) {
    // Sort loops in the current function by the line of their starting
    // location in the source code.
    std::vector<std::pair<int, Loop *>> SortedLoops;
    std::set<const Loop *> SeenLoops;

    for (auto B = F->begin(), BE = F->end(); B != BE; ++B) {
      Loop *L = li->getLoopFor(B);
      if (L && !SeenLoops.count(L)) {
        SeenLoops.insert(L);
        SortedLoops.emplace_back(L->getStartLoc()->getLine(), L);
      }
    }

    std::sort(SortedLoops.begin(), SortedLoops.end());

    std::string FunctionName = F->getName();

    // Iterate over sorted loops and annotate the ones that are referenced in
    // `ParallelLoopsIndexes`.
    for (unsigned I = 0, Size = SortedLoops.size(); I != Size; ++I) {
      if (ParallelLoops->count(I)) {
        errs() << "Found parallel loop in file " << FileName
               << ", line " << SortedLoops[I].second->getStartLoc()->getLine()
               << "\n";
        setMetadataParallelLoop(SortedLoops[I].second);
      }
    }
  }
}

void AnnotateParallel::readFunctionDebugInfo(Module &M) {
  Finder.processModule(M);

  for (auto It : Finder.subprograms()) {
    FunctionDebugInfo[It->getFunction()] = &*It;
  }
}

bool AnnotateParallel::runOnModule(Module &M) {
  // Read file and denotate loops as parallel or not.
  readFile();
  readIndexesFile();
  readFunctionDebugInfo(M);

  // Try to find in module target functions.
  for (Module::iterator F = M.begin(), FE = M.end(); F != FE; F++) {
    if (F->isDeclaration() || F->isIntrinsic())
      continue;
    this->li = &getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo();
    functionIdentify(F);
  }
  // Clear used functions.
  Functions.erase(Functions.begin(), Functions.end());
  return true;
}

char AnnotateParallel::ID = 0;
static RegisterPass<AnnotateParallel> Z("annotateParallel",
"Mark loops as Parallel.");

//===------------------------ annotateLoopParallel.cpp --------------------===//
