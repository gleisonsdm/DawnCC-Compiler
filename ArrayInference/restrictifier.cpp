//===--------------------------- restricfier.cpp --------------------------===//
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

#include <fstream>

#include <stack>
#include <queue>
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

#include "PtrRangeAnalysis.h"

#include "restrictifier.h"

using namespace llvm;
using namespace std;
using namespace lge;

#define DEBUG_TYPE "restrictifier"

static cl::opt<bool> ClEmitRest("Restrictifier",
    cl::Hidden, cl::desc("Use the infrastructure to clone loops."));

bool Restrictifier::isOMP () {
  return omp;
}

void Restrictifier::setTrueOMP () {
  this->omp = true;
}

void Restrictifier::setFalseOMP () {
  this->omp = false;
}

bool Restrictifier::isValid () {
  return this->valid;
}

void Restrictifier::setValidTrue () {
  this->valid = true;
}

void Restrictifier::setValidFalse () {
  this->valid = false;
}

std::string Restrictifier::getName () {
  return NAME;
}

void Restrictifier::setName (std::string name) {
  NAME = name;
}

void Restrictifier::identifyOffsets (std::string str) {
  unsigned int index = 0, ie = 0;

  for (index = 0, ie = str.size(); index != ie; index++)
    if (str[index] == '#')
      break;

  if (isOMP()) {
    for (index++; index != ie; index++)
      if (str[index] == '#')
        break;
  }

  for (; index != ie; index++)
    if (str[index] == '(') {
      index++;
      break;
    }

  for (; index != ie; index++) {
    if (str[index] == ')')
      break;
    if (str[index] == ',')
      continue;
    
    std::string name = std::string();
    std::string lower = std::string();
    std::string upper = std::string();
    
    while (str[index] != '[') {
      name += str[index];
      index++;
    }
    index++;

    while (str[index] != ':') {
      lower += str[index];
      index++;
    }
    index++;

    while (str[index] != ']' ||
           !(str[(index+1)] == ',' || str[(index+1)] == ')')) {
      upper += str[index];
      index++;
    }
    limits[name] = std::make_pair(lower, upper);
  }
}

void Restrictifier::getBounds (std::map<std::string, std::string> & lowerB,
                               std::map<std::string, std::string> & upperB,
                               std::map<std::string, Value*> pointersB,
                               std::map<std::string, bool> needR) {
  for (auto I = lowerB.begin(), IE = lowerB.end(); I != IE; I++) {
    limits[I->first] = std::make_pair(I->second, upperB[I->first]);
    pointers[I->first] = pointersB[I->first];
    needRef[I->first] = needR[I->first];
  }
}

std::string Restrictifier::generateRestrict (std::string varA, std::string varB) {
  std::string varAA = ((needRef[varA]) ? ("&" + varA) : (varA));
  std::string varBB = ((needRef[varB]) ? ("&" + varB) : (varB));
  std::string str = std::string();
  str += NAME + " |= ";
  str += "!(((void*) (" + varAA + " + " + limits[varA].first + ") > ";
  str += "(void*) (" + varBB + " + " + limits[varB].second + "))\n|| ";
  str += "((void*) (" + varBB + " + " + limits[varB].first + ") > ";
  str += "(void*) (" + varAA + " + " + limits[varA].second + ")));\n";
  return str;
}

std::string Restrictifier::disambiguatePointers () {
  std::string desambiguateStr = std::string();
  desambiguateStr = "char " + NAME + " = 0;\n";
  
  for (auto I = limits.begin(), IE = limits.end(); I != IE; I++)
    for (auto J = I, JE = IE; J != JE; J++) {
        
      if (J != I)
          desambiguateStr += generateRestrict(I->first, J->first);
      }
  return desambiguateStr;
}

std::string Restrictifier::changePragmas (std::string pragmas) {
  std::string pragChg = std::string();
  unsigned int index = 0, ie = 0;
  
  for (index = 0, ie = pragmas.size(); index != ie; index++) {
    if (pragmas[index] == '#')
      break;
    pragChg += pragmas[index];
  }
  
  pragChg += disambiguatePointers();

  for (;index != ie; index++) {
    if (pragmas[index] == '\n')
      pragChg += " if(!" + NAME + ")";
    pragChg += pragmas[index];
  }

  if (!isValid()) {
    return pragmas;
  }

  return pragChg;
}

std::string Restrictifier::generateTests (std::string pragmas) {
  if (!ClEmitRest) {
    setValidFalse();
    return pragmas;
  }

  //identifyOffsets(pragmas);

  if (limits.size() < 2) {
    setValidFalse();
    return pragmas;
  }

  return changePragmas(pragmas);
}

//===--------------------------- restricfier.cpp --------------------------===//
