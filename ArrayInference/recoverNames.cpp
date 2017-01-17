//===------------------------------- RecoverNames.cpp ---------------------===//
//
// This file is distributed under the UFMG -
// Universidade Federal de Minas Gerais
// It has a Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//                           The LLVM Compiler Infrastructure
//
// This pass return the original name of variables for each Instruction present
// in source file.
// But, if is a memory access instruction, return the name in source file.
// The result is the list of names per region.
//
// To use this pass please use the flag "-RecoverNames", see the example
// available below:
//
// ./opt -load ${LIBR}/libLLVMArrayInference.so -RecoverNames ${BENCH}/$2.bc
//
// The ambient variables and your signification:
//   -- LIBR => Set the location of ArrayInference tool location.
//   -- BENCH => Set the benchmark's paste.
//
//===----------------------------------------------------------------------===//

#include <map>

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/RegionIterator.h"

#include "llvm/Analysis/ScalarEvolutionExpressions.h"

#include "recoverNames.h"

using namespace llvm;

#define DEBUG_TYPE "RecoverNames"

Value *RecoverNames::getBasePtrValue(Instruction *inst, const Region *r) {
  if (!isa<AllocaInst>(&(*inst)) && !isa<LoadInst>(&(*inst)) &&
      !isa<StoreInst>(&(*inst)) && !isa<GetElementPtrInst>(&(*inst)))
    return nullptr;

  if (isa<AllocaInst>(&(*inst)))
    return (&(*inst));

  Value *ptr = inst;
  while (isa<StoreInst>(ptr) || isa<LoadInst>(ptr) ||
         isa<GetElementPtrInst>(ptr)) {
    if (getOriginalName(ptr) != "")
      break;
    if (StoreInst *ST = dyn_cast<StoreInst>(ptr))
      ptr = ST->getPointerOperand();
    else if (LoadInst *LD = dyn_cast<LoadInst>(ptr))
      ptr = LD->getPointerOperand();
    else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(ptr))
      ptr = GEP->getPointerOperand();
  }
  return ptr;
}

Value* RecoverNames::getPointerFnCall (CallInst *CI) {
  Function *F = CI->getParent()->getParent();
  if (callPtrList.F && callPtrList.F == F)
    return callPtrList.pointers[CI];
  
  // If the function has not analyzed, analyze it and try
  // find all uses of the calls.
  callPtrList.F = F;
  callPtrList.pointers.erase(callPtrList.pointers.begin(),
                            callPtrList.pointers.end());
  for (auto I = F->begin(), IE = F->end(); I != IE; I++)
    for (auto J = I->begin(), JE = I->end(); J != JE; J++)
      if (BitCastInst *BI = dyn_cast<BitCastInst>(J))
        if (CallInst *CI = dyn_cast<CallInst>(BI->getOperand(0)))
          callPtrList.pointers[CI] = BI;
  
  return callPtrList.pointers[CI];
}

const Function *RecoverNames::findEnclosingFunc(const Value *V) {
  if (const Argument *Arg = dyn_cast<Argument>(V))
    return Arg->getParent();

  if (const Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent()->getParent();

  return NULL;
}

const DILocalVariable *RecoverNames::findVar(const Value *V, const Function *F) {
  for (auto Iter = inst_begin(F), End = inst_end(F); Iter != End; ++Iter) {
    const Instruction *I = &*Iter;
    if (const DbgDeclareInst *DbgDeclare = dyn_cast<DbgDeclareInst>(I)) {
      if (DbgDeclare->getAddress() == V)
        return DbgDeclare->getVariable();

    }
    else if (const DbgValueInst *DbgValue = dyn_cast<DbgValueInst>(I))
      if (DbgValue->getValue() == V)
        return DbgValue->getVariable();
  }
  return NULL;
}

StringRef RecoverNames::getOriginalName(const Value *V) {
  const llvm::Function *F = findEnclosingFunc(V);
  if (!F)
    return "";

  const llvm::DILocalVariable *Var = findVar(V, F);
  if (!Var)
    return "";

  return Var->getName();
}

void RecoverNames::initializeVarNames(VarNames *var, Instruction *I,
                                      const Region *r) {
  var->isLocal = true;
  var->isGlobal = false;
  var->value = getBasePtrValue(I, r);
  typeVarNames(var, I);
  if (AllocaInst *AI = dyn_cast<AllocaInst>(&(*var->value))) {
    var->arraySize = AI->getArraySize();
    var->type = AI->getType();
  }
}

void RecoverNames::typeVarNames(VarNames *var, Instruction *I) {
  var->isStore = false;
  var->isLoad = false;
  var->isAlloca = false;
  if (isa<AllocaInst>(&(*I)))
    var->isAlloca = true;
  else if (isa<StoreInst>(&(*I)))
    var->isStore = true;
  else if (isa<LoadInst>(&(*I)))
    var->isLoad = true;
}

bool RecoverNames::addVarName(RegionVars *list, VarNames var) {
  unsigned int i = 0;
  for (unsigned int e = list->variables.size(); i < e; i++)
    if (list->variables[i].name == var.name &&
        list->variables[i].nameInFile == var.nameInFile)
      return false;

  if (i == list->variables.size())
    list->variables.push_back(var);

  return true;
}

bool RecoverNames::haveListLocation(Region *region) {
  unsigned int loc = 0;
  if (varsList.empty())
    return false;

  while (varsList[loc].region != region) {
    loc++;
    if (loc == varsList.size())
      return false;
  }
  return true;
}

unsigned int RecoverNames::getListLocation(Region *region) {
  if (varsList.empty())
    return INT_MAX;
  unsigned int loc = 0;
  while (varsList[loc].region != region) {
    loc++;
    if (loc == varsList.size())
      break;
  }
  return loc;
}

// This void copy one list of Variables of Analyzed region
// to one new list menber, example:
//
//               ---------------    -----------
//               || a , b , c || => || d , e ||
//               ---------------    -----------
//
//   The result is a new region, with all variable to the last set
//   ,the  (d,e) set.
//
//             -----------------------
//             || a , b , c , d , e ||
//             -----------------------
//
void RecoverNames::copyList(RegionVars *list, int regionLocation) {
  for (unsigned int j = 0, k = 0,
                    je = varsList[regionLocation].variables.size();
       j < je; j++) {
    VarNames var;
    var = varsList[regionLocation].variables[j];
    // For each variable, if set not contains
    // insert in the set.
    for (; k < list->variables.size(); k++)
      if (list->variables[k].name == var.name &&
          list->variables[k].nameInFile == var.nameInFile)
        break;

    if (k == list->variables.size())
      list->variables.push_back(varsList[regionLocation].variables[j]);
  }
}

void RecoverNames::getPtrMetadata(RegionVars *list, Instruction *J,
                                  Instruction *I, Region *r) {
  std::string nameInFile = getOriginalName(I);

  if (nameInFile != std::string()) {
    VarNames var;
    var.nameInFile = nameInFile;
    // If the instruction is a memory access instruction.
    // If yes, we can insert your name:

    if (LoadInst *LI = dyn_cast<LoadInst>(&(*I)))
      initializeVarNames(&var, LI, r);

    else if (StoreInst *ST = dyn_cast<StoreInst>(&(*I)))
      initializeVarNames(&var, ST, r);

    else if (AllocaInst *AL = dyn_cast<AllocaInst>(&(*I)))
      initializeVarNames(&var, AL, r);

    addVarName(list, var);
  }

  // If a global variable is found, insert in the list.
  if (LoadInst *LI = dyn_cast<LoadInst>(&(*J)))
    if (isa<GetElementPtrInst>(&(*I)))
      for (unsigned int i = 0, e = listGlobalVars.size(); i < e; i++)
        if (J->getOperand(0)->getName() == listGlobalVars[i].name) {
          VarNames var;
          initializeVarNames(&var, LI, r);
          var.nameInFile = listGlobalVars[i].name;
          var.globalValue = listGlobalVars[i].value;
          var.isLocal = false;
          var.isGlobal = true;
          addVarName(list, var);
          break;
        }
}

void RecoverNames::initializeRegionVars(RegionVars *list, Region *region,
                                        Region *regionParent, bool isTopRegion,
                                        bool hasParent, int id) {
  list->region = region;
  list->regionParent = regionParent;
  list->IsTopRegion = isTopRegion;
  list->hasParent = hasParent;
  list->regionId = id;
  list->regionName = region->getNameStr();
}

void RecoverNames::findRegionAdress(Region *region, Region *regionParent,
                                    int *id) {
  RegionVars list;
  // Insert in list the data of analized region.
  if (*id != 0)
    initializeRegionVars(&list, region, regionParent, false, true, *id);
  else
    initializeRegionVars(&list, region, regionParent, true, false, *id);

  // Try find a name of variables for each instruction in a basic block.
  for (Region::block_iterator B = region->block_begin(),
                              BE = region->block_end();
       B != BE; ++B)
    for (auto I = B->begin(), J = B->begin(), IEnd = B->end(); I != IEnd; ++I) {
      getPtrMetadata(&list, J, I, region);
      J = I;
      VarNames var;
      var = getNameofValue(I);
    }

  // Copy the parant variables
  if (haveListLocation(region))
    copyList(&list, getListLocation(region));

  // Insert this region, now analized, in the set of regions
  varsList.push_back(list);

  // For each sub region, try find your variable names
  for (auto SR = region->begin(), SRE = region->end(); SR != SRE; ++SR) {
    Region *r = &(**SR);
    *id++;
    findRegionAdress(r, region, id);
  }
}

RecoverNames::RegionVars RecoverNames::findRegionVariables(Region *R) {
  for (unsigned int i = 0, e = varsList.size(); i < e; i++)
    if (varsList[i].region == R)
      return varsList[i];

  RegionVars list;
  initializeRegionVars(&list, R, R, false, false, -1);
  list.regionName = "This region variables not found.\n";
  return list;
}

RecoverNames::VarNames RecoverNames::getName(Instruction *I) {
  BasicBlock *bb = I->getParent();
  Region *r = rp->getRegionInfo().getRegionFor(bb);
  RegionVars rv = findRegionVariables(r);
  Value *v = getBasePtrValue(I, r);

  for (unsigned int i = 0, e = rv.variables.size(); i < e; i++)
    if (rv.variables[i].value == v)
      return rv.variables[i];

  VarNames vn;
  return vn;
}

void RecoverNames::searchGlobalVariables(Module *M) {
  if (listGlobalVars.empty()) {
    // If don't know the global Variables, do the search on module metadata.
    NamedMDNode *MD = M->getNamedMetadata("llvm.dbg.cu");
    if (MD == nullptr)
      return;
    for (NamedMDNode::op_iterator Op = MD->op_begin(), Ope = MD->op_end();
         Op != Ope; ++Op) {
      MDNode* ND = *(Op);
      if (DICompileUnit *CU = dyn_cast<DICompileUnit>(ND)) {
        DIGlobalVariableArray DG = CU->getGlobalVariables(); 
        for (auto I = DG.begin(), IE = DG.end(); I != IE; ++I) {
          if (DIGlobalVariable *DGV = dyn_cast<DIGlobalVariable>(*I)) {
            GlobVars tempvar;
            tempvar.name = DGV->getName();
            tempvar.value = DGV;
            listGlobalVars.push_back(tempvar);
          }
        }
      }
    }
  }
}

RecoverNames::VarNames RecoverNames::getNameofValue(Value *V) {
  VarNames var;
  var.nameInFile = "";
  if (isa<Argument>(V) || isa<PHINode>(V)) {
    var.nameInFile = getOriginalName(V);
    return var;
  }

  // Return if the based pointer of instruction is a global variable.
  if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    Module *M = GV->getParent();
    searchGlobalVariables(M);
    for (unsigned int i = 0, e = listGlobalVars.size(); i < e; i++)
      if (V->getName() == listGlobalVars[i].name) {
          var.nameInFile = listGlobalVars[i].name;
          var.globalValue = listGlobalVars[i].value;
          var.isLocal = false;
          var.isGlobal = true;
          break;
        }
  }


  if (isa<Instruction>(V)) {
    Instruction *I = cast<Instruction>(V);
    if (CallInst *CI = dyn_cast<CallInst>(I)) {
      Value* PTR = getPointerFnCall(CI);
      var.nameInFile = std::string();
      var.nameInFile = getOriginalName(CI);
      if (PTR == NULL)
        return var;
      if (var.nameInFile == std::string())
        var.nameInFile = getOriginalName(PTR);
      return var;
    }


    if (!isa<AllocaInst>(I) && !isa<LoadInst>(I) && !isa<StoreInst>(I) &&
        !isa<GetElementPtrInst>(I) && !isa<GlobalValue>(I)) {
      var.nameInFile = getOriginalName(V);
      return var;
    }

    BasicBlock *bb = I->getParent();
    Function *F = bb->getParent();
    Region *r = rp->getRegionInfo().getRegionFor(bb);
    RegionVars rv = findRegionVariables(r);
    Value *v = getBasePtrValue(I, r);
    Module *M = F->getParent();

    if (isa<Argument>(v)) {
      var.nameInFile = getOriginalName(v);
      return var;
    }

    if (v == nullptr)
      return var;

    // If based pointer is a Alloca, Return for it.
    if (AllocaInst *AI = dyn_cast<AllocaInst>(v)) {
      var.nameInFile = getOriginalName(AI);
      initializeVarNames(&var, I, r);
      return var;
    }

    if (isa<LoadInst>(v) || isa<StoreInst>(v)) {
      var.nameInFile = getOriginalName(v); 
      initializeVarNames(&var, I, r);
      return var;
    }

    if (isa<GlobalValue>(I)) {
      var.nameInFile = getOriginalName(I);
      return var;
    }

    // Return if the based pointer of instruction is a global variable.
    if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
      for (unsigned int i = 0, e = listGlobalVars.size(); i < e; i++)
        if (I->getOperand(0)->getName() == listGlobalVars[i].name) {
          var.nameInFile = listGlobalVars[i].name;
          var.globalValue = listGlobalVars[i].value;
          var.isLocal = false;
          var.isGlobal = true;
          break;
        }
    }
  }
 
  return var;
}

bool RecoverNames::runOnFunction(Function &F) {

  li = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  rp = &getAnalysis<RegionInfoPass>();
  aa = &getAnalysis<AliasAnalysis>();
  se = &getAnalysis<ScalarEvolution>();

  // If Global Variables isn't search at now (or not found), do the search:
  Module *M = F.getParent();
  searchGlobalVariables(M);

  int index = 0;

  // For top region in the function, call the void findRegionAdress:
  Region *topRegion = rp->getRegionInfo().getRegionFor(&F.getEntryBlock());

  findRegionAdress(topRegion, topRegion, &index);

  return true;
}

char RecoverNames::ID = 0;

static RegisterPass<RecoverNames>
X("RecoverNames", "Recover of the pointer name in source file.");

void RecoverNames::getAnalysisUsage(AnalysisUsage &AU) const { 
  AU.addRequired<RegionInfoPass>();
  AU.addRequired<AliasAnalysis>();
  AU.addRequired<ScalarEvolution>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

//===------------------------------- RecoverNames.cpp ---------------------===//
