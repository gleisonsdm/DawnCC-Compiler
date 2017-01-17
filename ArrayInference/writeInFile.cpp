//===------------------------ writeInFile.cpp --------------------------===//
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
// opt -load ${LIBR}/libLLVMArrayInference.so -writeInFile -Emit-GPU=OPTN \
//  ${BENCH}/$2.bc
//
// The ambient variables and your signification:
//   -- LIBRNIKE => Set the location of NIKE library.
//   -- BENCH => Set the benchmark's paste.
//   -- OPTN => set as true to run just "GPU__" functions, and false to run
//      all functions.
// 
//===----------------------------------------------------------------------===//
#include <fstream>

#include "llvm/IR/Module.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

#include "writeInFile.h" 

#define CarriageReturn 13

using namespace llvm;
using namespace std;
using namespace lge;

#define DEBUG_TYPE "writeInFile"

static cl::opt<bool> ClEmitGPU("Emit-GPU",
cl::Hidden, cl::desc("Analyse just 'GPU__' functions."));

static cl::opt<bool> ClRun("Run-Mode",
cl::Hidden, cl::desc("Annotate parallel loops or tasks"));

StringRef WriteInFile::getFileName(Instruction *I) {
  MDNode *Var = I->getMetadata("dbg");
  if (Var)
    if (DILocation *DL = dyn_cast<DILocation>(Var))
      return DL->getFilename();
  return std::string();
}

int WriteInFile::getLineNo(Value *V) {
  if(!V)
    return -1;
  if (Instruction *I = dyn_cast<Instruction>(V))
      if (MDNode *N = I->getMetadata("dbg"))
        if (DILocation *DL = dyn_cast<DILocation>(N))
          return DL->getLine();
  return -1;
}

std::string WriteInFile::getNameofFile(const Value *V) {
if (const Instruction *I = dyn_cast<Instruction>(&(*V)))
  if (MDNode *N = I->getMetadata("dbg"))
    if (DILocation *DL = dyn_cast<DILocation>(N))
      return DL->getFilename();
return std::string();
}

std::string WriteInFile::getLineForIns(Value *V) {
  if (Instruction *I = dyn_cast<Instruction>(V))
    if (MDNode *N = I->getMetadata("dbg"))
      if (DILocation *Loc = dyn_cast<DILocation>(N)) {
        unsigned Line = Loc->getLine();
        StringRef File = Loc->getFilename();
        StringRef Dir = Loc->getDirectory();
        //DIVariable Var(N);
        std::fstream FileStream;
        FileStream.open((Dir.str() + "/" + File.str()).c_str(), std::ios::in);
        for (unsigned i = 0; i < Line - 1; ++i)
           FileStream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::string line;
        std::getline(FileStream, line);
        return line;
      }
  return std::string();
}

void WriteInFile::addCommentToLine(std::string Comment, unsigned int Line) {
  Comments[Line] = Comment;
}

void WriteInFile::printToFile(std::string Input, std::string Output) {
fstream Infile(Input.c_str());
if (!Infile){
  errs() << "\nError. File " << Input << " has not found.\n";
  return;
}
std::string Line = std::string();
std::error_code EC; 
sys::fs::OpenFlags Flags = sys::fs::F_RW;
raw_fd_ostream File(Output.c_str(), EC, Flags);
errs() << "\nWriting output to file " << Output << "\n";

unsigned LineNo = 1;
while (!Infile.eof()) {
  Line = std::string();
  std::getline(Infile, Line);
  if (Comments.count(LineNo)) {
    std::string Start;
    // Gather all the blanks and tabs.
    for (std::string::iterator It = Line.begin(), E = Line.end(); It != E;
    ++It)
      if (*It == ' ' || *It == '\t')
        Start += *It;
      else
        break;

    // Emit the comments.
    if (Comments[LineNo].size() > 0)
      File << Start << Comments[LineNo][0];
    for (unsigned i = 1, ie = Comments[LineNo].size(); i != ie; ++i){
      if (Comments[LineNo][i-1] == '\n')
        File << Start;
      File << Comments[LineNo][i];
    }
  }
  // Try identify if console has add a Carriage Return character in the
  // end of the string.
  if (Line[Line.size() - 1] == CarriageReturn)
    Line.erase(Line.end() - 1, Line.end());
  
  Line += "\n";
  File << Line;
  LineNo++;
}
File.close();
}


void WriteInFile::copyComments(std::map <unsigned int, std::string> CommentsIn){
for(auto I = CommentsIn.begin(), E = CommentsIn.end(); I != E; ++I)
   addCommentToLine(I->second,I->first);
}

void WriteInFile::addComments (Instruction *I, std::string comment) {
int Line = getLineNo(I);
string Comment;
raw_string_ostream Stream(Comment);
Stream << comment;
if(Line != -1)
addCommentToLine(Stream.str(), Line);
}

std::string WriteInFile::generateOutputName (std::string fileName) {
  std::string key (".");
  std::size_t found = fileName.rfind(key);
  if (found != std::string::npos)
    fileName.replace(found,key.length(),"_AI.");
  return fileName;
}

bool WriteInFile::findModuleFileName (Module &M) {
for (auto F = M.begin(), FE = M.end(); F != FE; ++F)
  for (auto B = F->begin(), BE = F->end(); B != BE; ++B)
    for (auto I = B->begin(), IE = B->end(); I != IE; ++I) {
      InputFile = getFileName(&(*I));
      if(!InputFile.empty())
        return true;
    }
return false;
}

bool WriteInFile::findFunctionFileName (Function &F) {
  for (auto B = F.begin(), BE = F.end(); B != BE; ++B)
    for (auto I = B->begin(), IE = B->end(); I != IE; ++I) {
      InputFile = getFileName(&(*I));
      if(!InputFile.empty())
        return true;
    }
return false;
}

bool WriteInFile::runOnModule (Module &M) {
if (!findModuleFileName(M))
  return true;

std::string lInputFile = InputFile;
for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) { 
  if (ClEmitGPU) {
    std::string flag = F->getName();
    flag.erase(flag.begin() + 5, flag.end());
    if (flag != "GPU__")
      continue;
  }

  if (F->isDeclaration() || F->isIntrinsic()) {
     continue;
  }

  if (!findFunctionFileName(*F))
    continue;

  // If has found a new file to input information in this module,
  // write the last module available with information..
  if (lInputFile != InputFile) {
    printToFile(lInputFile, generateOutputName(lInputFile));
    lInputFile = InputFile;
    Comments.erase(Comments.begin(), Comments.end());
  }

  if (ClRun) {
    this->re = &getAnalysis<RecoverExpressions>(*F);
    copyComments(this->re->Comments);
  }
  else {
    this->we = &getAnalysis<WriteExpressions>(*F);
    copyComments(this->we->Comments);
  }
}   

printToFile(InputFile, generateOutputName(InputFile));
return false;
}

char WriteInFile::ID = 0;
static RegisterPass<WriteInFile> Z("writeInFile",
"Write comments in source file.");


//===-------------------------- writeInFile.cpp --------------------------===//

