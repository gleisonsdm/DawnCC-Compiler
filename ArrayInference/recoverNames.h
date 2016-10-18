//===------------------------------- RecoverNames.h ----------------------===//
//
//                           The LLVM Compiler Infrastructure
//
// This file is distributed under the UFMG - 
// Universidade Federal de Minas Gerais
// It has a Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass return the original name of variables for each Instruction present
// in source file.
// But, if is a memory access instruction, return the name in source file.
// The result is the name per region.
// 
// To use this pass please use the flag "-writeExpressions", see the example
// available below:
//
// ./opt -load ${LIBR}/libLLVMArrayInference.so -RecoverNames ${BENCH}/$2.bc 
//
// The ambient variables and your signification:
//   -- LIBR => Set the location of ArrayInference tool location.
//   -- BENCH => Set the benchmark's paste.
// 
//===----------------------------------------------------------------------===//
#include <vector>

#include "llvm/IR/DIBuilder.h"

// Start of llvm's namespace.
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

// Start of the RecoverNames struct.
class RecoverNames : public FunctionPass {

  public:

  // Structs defined to the pass.

  typedef struct VarNames{
    std::string name;
    std::string nameInFile;
    Value* value;
    DIGlobalVariable* globalValue;
    bool isLocal;
    bool isGlobal;
    bool isLoad;
    bool isStore;
    bool isAlloca;
    Value* arraySize;
    PointerType* type;
  } VarNames;

  typedef struct GlobVars{
    std::string name;
    DIGlobalVariable* value;
  } GlobVars;

  typedef struct RegionVars{
    std::vector <VarNames> variables;
    std::vector <GlobVars> globalVars;
    Region *region;
    std::string regionName;
    int regionId;
    Region *regionParent;
    bool hasParent;
    bool IsTopRegion;
  } RegionVars;
  
  typedef struct CallPointers{
    std::map<CallInst*,BitCastInst*> pointers;
    Function *F;
  } CallPointers;

  // End of structs's definition.

  static char ID;

  explicit RecoverNames() : FunctionPass(ID) {};

  // This method return the set of variables of the region R. If not found,
  // return a empty Set, but the regionName attribute is a message.
  RegionVars findRegionVariables(Region *R);

  // Return the varNames for instruction I, case was analyzed.
  VarNames getName(Instruction *I);

  // Set with calls and your bitcast instructions.
  CallPointers callPtrList;

  // Return the varNames for instruction I, independent of the pass run.
  // Implemented to facilitate for programmer that use this pass, in this
  // case, if the programmer does not want to run the pass.
  VarNames getNameofValue(Value *V);

  // For Function F, run the pass.
  virtual bool runOnFunction(Function &F) override;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  // Return the base pointer for callInst, if the function return a mamory
  // allocated.
  Value* getPointerFnCall (CallInst *CI);

  private:

  // Return if some instruction is invariant in a region.
  // A reference to function argument or constant value is invariant.
  bool isInvariant(const Value *Val, const Region *Reg, LoopInfo *LI,
  AliasAnalysis *AA);

  // Returns the base pointer for some memory instruction.
  // To load, store and alloca instruction.
  Value *getBasePtrValue(Instruction *inst, const Region *r);

  // Initialize a new regionVars list with the parameters received. 
  void initializeRegionVars(RegionVars *list, Region *region,
  Region *regionParent,bool isTopRegion, bool hasParent, int id);

  // Copy the list list of regionVars for "regionLocation" position in
  // varsList.
  // Used for example to know the parent variables that are lived in
  // some region.
  void copyList(RegionVars *list, int regionLocation);

  // Add variable var in a list, return true if it's added.
  bool addVarName(RegionVars *list,VarNames var);

  // Return if some region was analyzed before by pass.
  // This method return true if one region is not Analyzed.  
  bool haveListLocation(Region *region);

  // Return the position in vector varsList for Region "region".
  unsigned int getListLocation(Region *region);

  /// Initialize VarNames with default values.
  void initializeVarNames(VarNames *var, Instruction *I, const Region *r);

  // Set the type of operation for instruction I in variable var.
  void typeVarNames(VarNames *var, Instruction *I);

  // This pass is a function pass. For this, each Region  is analyzed by pass
  // in its  function. The idea is do a liveness analyses.
  void findRegionAdress(Region *region,Region *regionParent,int *idRegion);

  // getPtrMetadata void is used to insert in the list of "regionVars"
  // the name of variables.
  void getPtrMetadata(RegionVars *list,Instruction *J,Instruction *I,
                      Region *r);

  // Return the Function of the value v.
  const Function* findEnclosingFunc(const Value* V);
  
  // This method return a MDNode just if some instructions have a address or
  // value is equal to the value v.
  const DILocalVariable* findVar(const Value* V,const Function* F);

  // Return the name of the variable if it is interesting to analyze.  
  StringRef getOriginalName(const Value* V);

  // Search in the Module the Global Variables.
  void searchGlobalVariables(Module *M);

  // Global data structs used.
  std::vector <RegionVars> varsList;

  std::vector <GlobVars> listGlobalVars;
  
  int regionGlobalIndex = 0;

  std::map<Value*, bool> arrays; 
  // End of global data structs used.

  // Passes used.
  RegionInfoPass *rp;
  AliasAnalysis *aa;
  ScalarEvolution *se;
  LoopInfo *li;
  //End of "passes used" declare region.
};// End of the RecoverNames class.

}//End of llvm's namespace.
//===------------------------------- RecoverNames.h ----------------------===//
