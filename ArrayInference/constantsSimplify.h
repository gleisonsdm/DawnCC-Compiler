//===---------------------------- ConstantsSimplify.h ---------------------===//      
// This file is distributed under the UFMG -                                     
// Universidade Federal de Minas Gerais                                          
// It has a Open Source License. See LICENSE.TXT for details.                    
//                                                                               
//===----------------------------------------------------------------------===// 
//                                                                               
//                           The LLVM Compiler Infrastructure                    
//                                                                               
// The constantsSimplify tool can solve and simplify a LLVM's contant
// expressions, present in IR, but the class return a numeric (integer or real
// number) correspondent to the expression available. If the result of this
// analisys isn't correct, the function named "isValid" will return false, 
// in the other case will return true.                                  
//                                                                               
// To use this tool, please create a object of it and use, like the example         
// available below:                                                              
//                                                                               
// if (Constant *C = dyn_cast<Constant>(&(*V))) {
// ConstantsSimplify 
// long long int value = CS.getUniqueConstantInteger(C,DT);                     
//   if (CS.isValid())                                                            
//       return std::to_string(value);                                              
//     setValidFalse();                                                             
//     return "0";                                                                  
//   }                                                   
//                                                                               
//===----------------------------------------------------------------------===//
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

namespace llvm{

class Value;
class Instruction;

class ConstantsSimplify {

  private :

  // Data structs used
  bool Valid;
  Value *PointerValue;

  // Functions to change the status of Analysis.
  void setValidTrue ();
  void setValidFalse ();

  // Set the Pointr as nullpt
  void setPointerNull();
  
  // Set the target Pointer of Analysis.
  void setPointer (Value *V);

  // Return the Pointer used as reference by class.
  Value *getPointer ();


  // Return the numeric value to specific Constant type in LLVM IR.
  double getConstant (Constant *C, const DataLayout *DT);
  double getConstantAggregateZero (const ConstantAggregateZero *C) const;
  double getConstantArray (const ConstantArray *C);
  double getConstantDataSequential (const ConstantDataSequential *C);
  double getConstantDataArray (const ConstantDataArray *C);
  double getConstantDataVector (const ConstantDataVector *C);
  double getConstantExpr (ConstantExpr *C, const DataLayout *DT);
  double getConstantFP (const ConstantFP *C);
  double getConstantInt (const ConstantInt *C) const;
  double getConstantPointerNull (ConstantPointerNull *C, const DataLayout *DT);
  double getConstantStruct (const ConstantStruct *C);
  double getConstantVector (const ConstantVector *C);
  
  // Return the result of Compare Instruction if it's a constant expression.
  bool getConstantCmp (Constant *CI, const DataLayout *DT);
  
  // Return the subType of Type tpy. Look at this example:
  //   [1000 * i32]
  // The return is position * 4 (value in bytes)
  Type* getInternalType (Type *tpy, int position, const DataLayout *DT);
  
  public :

  // Return the bitwidht of type using DataLayout.
  unsigned int getSizeToType (Type *tpy, const DataLayout *DT);
  unsigned int getSizeToTypeInBits (Type *tpy, const DataLayout *DT);

  // Return the numeric value as Double.
  double getUniqueConstantNumber (Constant *C, Value *Pointer,
                                  const DataLayout *DT);

  // Return the numeric value as Integer.
  int getUniqueConstantInteger (Constant *C, Value *Pointer,
                                const DataLayout *DT);

  // Return true if is possible convert LLVM's constant to number.
  bool isValid ();

  // Return a correct Type size, if possible.
  long long int getFullSizeType(Type *tpy, const DataLayout *DT);
  
};// End of ConstantsSimplify class

}// End of llvm namespace
//===---------------------------- ConstantsSimplify.h ---------------------===//
