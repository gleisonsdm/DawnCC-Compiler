//===--------------------------- restricfier.h --------------------------===//
//
// This file is distributed under the Universidade Federal de Minas Gerais - 
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2015   Gleison Souza Diniz Mendon?a
//
//===----------------------------------------------------------------------===//
//
// Restricfier is a tool to restrictify the pointers when Array Inference 
// inserts pragmas in the source file.
// Using a vector with memory bounds and the name of each pointer, this
// can create and insert alias checks in a string format.
// 
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace lge;

namespace llvm {

class Restrictifier {

  private:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
    std::map<std::string, std::pair<std::string,std::string> > limits;

    std::map<std::string, Value*> pointers;
  
    std::string NAME;
  
    bool valid;
  
    bool omp;
  //===---------------------------------------------------------------------===

  // To Know if the result of this Analysis is valid.
  void setValidTrue ();
  void setValidFalse ();

  // Analyze and use all generated offsets, and associate with the variable;
  void identifyOffsets (std::string str);

  // Generates all computation to desambiguate pointers.
  std::string disambiguatePointers ();

  // Change the pragmas to add the condition to sent data between devices.
  std::string changePragmas (std::string pragmas);

  // generate overlap tests between two pointers.
  std::string generateRestrict (std::string varA, std::string varB);

  public:
  // Manipulates the name of restrictfier computation.
  std::string getName();
  void setName(std::string name);

  // To know if the used pragma is OMP.
  bool isOMP();
  void setTrueOMP();
  void setFalseOMP();

  // Generates all tests to analyze and meansure pointer overlaps.
  std::string generateTests(std::string pragmas);

  // Identify bounds for pointers.
  void getBounds (std::map<std::string, std::string> & lowerB,
                  std::map<std::string, std::string> & upperB,
                  std::map<std::string, Value*> pointersB);

  // Return true case resul of restrictifier is true, else in other case.
  bool isValid ();

  // Set tool
  Restrictifier () {
    NAME = "RESTRICTIFIER";
    valid = true;
    setFalseOMP();
    limits.erase(limits.begin(), limits.end());
  }
};

}

//===--------------------------- restricfier.h ----------------------------===//
