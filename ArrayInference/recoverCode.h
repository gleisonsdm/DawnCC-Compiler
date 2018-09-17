//===--------------------------- recoverCode.h ----------------------------===//
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
// RecoverCode is a class created for generate C code using  PtrRangeAnalysis
// to define the data limits used to access a pointer in some loop.
// In summary, this class translate the access expressions in LLVM's I.R. and
// use the original name of variables to write the correct parallel code.
//
// The name of variables generated by pass stay in NAME string. The 
// programmer can change this name in writeExpressions.h
//
//===----------------------------------------------------------------------===//
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include "PtrRangeAnalysis.h"

#include "constantsSimplify.h"
#include "recoverNames.h"

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

class RecoverCode {

  protected:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
  // Data structs to identify a instruction with the command with C/C++ sintaxy:
  std::map<std::string, int> commands;
  
  std::vector<std::string> Expression;
  
  std::map<Value*, std::pair<int,std::string> > ComputedValues;

  unsigned int NewVars;

  std::string NAME;

  RecoverNames *rn;

  bool Valid;

  char OMPF;
  
  Value *PointerValue;

  unsigned int numPHIRec;
  //===---------------------------------------------------------------------===

  // Insert the values after computate its solution
  void insertComputedValue (Value *V, int *id, std::string str);

  // Return true if value "V" has computed. If it is true, change the
  // variables "id" and "str". Return false in other case.
  bool selectComputedValue (Value *V, int *id, std::string *str);

  // Set "PointerValue" as null pointer.
  void setPointerNull(); 
  
  // Set "PointerValue" as "V".
  void setPointer (Value *V);
  
  // Return "PointerValue".
  Value *getPointer ();

  // Return the string present in the list of expressions named commands
  // If not found, return an empty string.
  std::string selectCommand(int var);

  // Return the name for used variable in instruction I.
  std::string getSextExp (SExtInst *I, std::string name, int *var,
                          const DataLayout *DT);

  // Return the name for used variable in instruction I.
  std::string getZextExp (ZExtInst *I, std::string name, int *var, 
                          const DataLayout *DT);

  // Return the generic expression (for instance, "add") of instruction I.
  std::string getGenericExp (Instruction *I, std::string name, int *var,
                      const DataLayout *DT);

  // Provides a implementation to TruncInst, support for short type
  std::string getTruncExp (TruncInst *TI, std::string name, int *var,
                      const DataLayout *DT);

  // Return the name of Instruction I.
  std::string getNameExp (Value *V, std::string name, int *var, const DataLayout *DT);

  // Return the expression for PtrToInt instruction.
  std::string getPtrToIntExp (PtrToIntInst *I, std::string name, int *var,
                       const DataLayout *DT);

  // Return the expression for IntToPtr instruction.
  std::string getIntToPtrExp (IntToPtrInst *I, std::string name, int *var,
                       const DataLayout *DT);

  // Return a expression for Select Instruction.
  std::string getSelExp (SelectInst *SI, std::string ptrName, int *var,
                  const DataLayout *DT);

  // Return, if possible, the string "value" in the integer "result".
  bool TryConvertToInteger(std::string value, long long int* result);

  // Return a expression for Compare Instruction.
  bool getCmpExp (ICmpInst *ICI, std::string ptrName, int *var,
                  const DataLayout *DT);

  // Retun the PHINode has a name, return it.
  std::string getPHINode (Value *V, std::string ptrName, int *var,
                          const DataLayout *DT);

  // Return true if called function is a Malloc
  bool isMallocCall (const CallInst *CI);

  // Return the Region for Basic Block bb.
  Region* regionofBasicBlock (BasicBlock *bb, RegionInfoPass*rp);

  // Set the result of pass as Invalid.
  void setValidTrue();
  void setValidFalse();

  // Return the sum of Sizes until "position".
  long long int getSumofTypeSize (Type* TPY, int position, const DataLayout* DT);

  // Return the Index to sum to based pointer of GetElementPtrInst.
  std::string getIndextoGEP (GetElementPtrInst*  GEP, std::string name, int *var,
                const DataLayout* DT);

  // Return if is possible recover the static size to the Pointer "V":
  bool isPointerMD(Value *V);

  // Provide the correct pointer computation to build the correct code.
  // Works for Loads and Stores.
  std::string getPointerMD (Value *V, std::string name, int *var,
                            const DataLayout* DT);

  // Return the subType of Type tpy. Look at this example:
  //   [1000 * i32]
  // The return is position * 4 (value in bytes)
  Type* getInternalType (Type *tpy, int position, const DataLayout *DT);  

  // Return a valid bounds to validate the tool's result.
  std::string getValidBounds (std::string Expression, int *Index);

  // Return the Expression value converted to the position of the array of
  // "Pointer"
  std::string getAccessExpression (Value* Pointer, Value* Expression,
                                  const DataLayout* DT, bool upper);

  // Generate pragmas to data transference between devices, using loop context.
  std::string getDataPragma (std::map<std::string, std::string> & vctLower,
                             std::map<std::string, std::string> & vctUpper,
                             std::map<std::string, char> & vctPtMA);

  // Generate pragmas to data transference between devices, using region
  // context. We can use this function trying to do memory coalescing.
  std::string getDataPragmaRegion (std::map<std::string, std::string> & vctLower,
                             std::map<std::string, std::string> & vctUpper,
                             std::map<std::string, char> & vctPtMA);

  // Generate the correct upper bound to each pointer analyzed.
  void generateCorrectUB (std::string lLimit, std::string uLimit,
                          std::string & olLimit, std::string & oSize);

  // Return case the pointer is defined inside a region (in this case,
  // we cannot annotate it).
  bool pointerDclInsideRegion(Region *R, Value *V);
  
  // Return case the pointer is defined inside a loop (in this case,
  // we cannot annotate it).
  bool pointerDclInsideLoop(Loop *L, Value *V); 

  public:

  RecoverCode () {
    this->NewVars = 0;
    this->NAME = "LLVM";
    this->Valid = false;
    this->numPHIRec = 10;
    restric = true;
  }
  //===---------------------------------------------------------------------===  
  //                              Data Structs                                   
  //===---------------------------------------------------------------------===
  std::map<unsigned int, std::string> Comments;

  bool restric;  
  //===---------------------------------------------------------------------===

  // Set true to emit omp pragmas
  void setOMP(char omp);

  // Return the stats of isOMP variable.
  char OMPType();

  // Used to set the string "NAME". This is the string with the loop
  // computation in source file. Please, don't use the same name for
  // loops in the same function.
  void setNAME (std::string name);

  // Return true if the result of Analisys is valid.
  bool isValid();

  // Return the bitWidth size to Type as a integer number.
  unsigned int getSizeToType (Type *tpy, const DataLayout *DT);
  
  // Return the bitWidth size to Value as a integer number.
  unsigned int getSizeToValue (Value *V, const DataLayout *DT);

  // Transforme some integer that represent a number of bits in
  // number of bytes. 
  unsigned int getSizeInBytes (unsigned int sizeInBits) const;

  // Clear the commands inserted until now.
  void clearCommands();

  // Insert the command in the list of expressions named commands.
  void insertCommand (int* var, std::string expression);

  // Validate the pointer to write the pragmas.
  bool isValidPointer (Value *Pointer, const DataLayout* DT);

  // Initialize the class when the programmer like.
  void initializeNewVars();

  // Set the RecoverNames to uses in this class.
  void setRecoverNames (RecoverNames *RN);

  // Try to simplify the Region, if possible.
  void simplifyRegion (Region *R, DominatorTree *DT, LoopInfo *LI, 
                          AliasAnalysis *AA);
  
  // Return a new Index for use.
  int getNewIndex ();

  // Clear the vector Expression.
  void clearExpression ();

  // Return the last index used.
  int getIndex ();
 
  // Return the vector Expression in one simple string.
  std::string getUniqueString ();

  // Return the access expression in a string form, to write in source file.
  std::string getAccessString (Value *V, std::string ptrName, int *var,
                              const DataLayout *DT);

  // Define if we need to dereference the pointer.
  bool needPointerAddrToRestrict(Value *V);

  // Return true for analyzable loop.
  bool analyzeLoop (Loop* L, int Line, int LastLine, PtrRangeAnalysis *ptrRA,
                    RegionInfoPass *rp, AliasAnalysis *aa, ScalarEvolution *se,
                    LoopInfo *li, DominatorTree *dt, std::string & test);

  // Return true for analyzable region.
  // TO DO : implement this function
  bool analyzeRegion (Region *r, int Line, int LastLine, PtrRangeAnalysis *ptrRA,
                    RegionInfoPass *rp, AliasAnalysis *aa, ScalarEvolution *se,
                    LoopInfo *li, DominatorTree *dt, std::string & test);

};

}

//===----------------------------- recoverCode.h --------------------------===//
