//===------------------------ writeInFile.h --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the Universidade Federal de Minas Gerais -
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2015   Gleison Souza Diniz Mendon?a
//
//===----------------------------------------------------------------------===//
//
// WriteExpressions is a optimization that insert into the source file every
// struct to data manipulation for use automatic parallelization.
// This pass write a set of instructions and annotations to write the correct
// parallel code in source file.
//
// The result is a new file with name that you need inform while call the pass.
// 
// To use this pass please use the flag "-writeInFile", see the example
// available below:
//
// opt -O1 -load ${LIBRNIKE}/PtrRangeAnalysis/libLLVMPtrRangeAnalysis.so \
// -load ${LIBRNIKE}/dbg/libPtrRangeAnalysisDBG.so \
// -load ${LIBR}/libLLVMArrayInference.so -writeInFile -Expression-File=FILENAME
//  ${BENCH}/$2.bc
//
// The ambient variables and your signification:
//   -- LIBRNIKE => Set the location of NIKE library.
//   -- LIBR => Set the location of ArrayInference tool location.
//   -- BENCH => Set the benchmark's paste.
//   -- FILENAME => Set the name of output file.
// 
//===----------------------------------------------------------------------===//

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"

#include "writeExpressions.h"
#include "recoverExpressions.h"

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
class ArrayInference;

class WriteInFile : public ModulePass {

  private:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
  std::map<unsigned int, std::string > Comments;

  std::string InputFile;
  //===---------------------------------------------------------------------===

  // getFilename
  // Returns the file that the value is declared in.
  StringRef getFileName(Instruction *I);

  // getLineNo
  // Returns the line of a value given by its debug information.
  int getLineNo(Value *V);

  // Insert Comments to some instruction I
  void addComments(Instruction *I, std::string comment);

  // Return a string with name of Value for Instruction in Value Val.
  std::string getNameofFile(const Value* V);

  // Return the number of the line for Instruction in Value V.
  std::string getLineForIns(Value *V);

  // To add a comment (Or some change that we need add other line in source
  // file)
  void addCommentToLine(std::string Comment, unsigned int Line);

  // To print Information in source file.
  void printToFile(std::string Input, std::string Output);

  // To copy the comments to local "Comments".
  void copyComments(std::map<unsigned int,std::string > CommentsIn);
  
  // Create a new name to write the output file
  std::string generateOutputName (std::string fileName);

  // Find the name of source file for Module M.
  // Return the first name of file found.
  bool findModuleFileName(Module &M);

  // Find the name of source file for Function F.
  bool findFunctionFileName (Function &F);

  public:

  static char ID;

  WriteInFile() : ModulePass(ID) {};
  
  // We need Insert the Instructions for each source file.
  virtual bool runOnModule(Module &M);

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<WriteExpressions>();
    AU.addRequired<RecoverExpressions>();
    AU.setPreservesAll();
  }
  
  // Pass that will insert comments in source file.
  WriteExpressions *we;

  // Insert tasks into the source file.
  RecoverExpressions *re;
  
};

}

//===------------------------ writeInFIle.h --------------------------===//
