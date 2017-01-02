//===---------------------------- ConstantsSimplify.cpp -------------------===//      
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

#include <cmath>
#include "llvm/Support/raw_ostream.h"
#include "constantsSimplify.h"
#include "llvm/Pass.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DataLayout.h"

using namespace llvm;

void ConstantsSimplify::setValidTrue () {
  this->Valid = true;
}

void ConstantsSimplify::setValidFalse () {
  this->Valid = false;
}

bool ConstantsSimplify::isValid () {
  return this->Valid;
}

void ConstantsSimplify::setPointerNull() {
  this->PointerValue = nullptr;
}

void ConstantsSimplify::setPointer (Value *V) {
  this->PointerValue = V;
}

Value *ConstantsSimplify::getPointer () {
  return this->PointerValue;
}

double ConstantsSimplify::getUniqueConstantNumber (Constant *C, Value *Pointer,
                                                   const DataLayout *DT) {
  setPointer(Pointer);
  setValidTrue();
  return getConstant(C, DT);
}

int ConstantsSimplify::getUniqueConstantInteger (Constant *C, Value *Pointer,
                                                 const DataLayout *DT) {
  setPointer(Pointer);
  setValidTrue();
  int value = getConstant(C, DT);
  return value;
}

// Generic method to solve all Complex Constants broken in 
// smallest sub problems.
double ConstantsSimplify::getConstant (Constant *C, const DataLayout *DT) {
  if (ConstantAggregateZero *CAZ = dyn_cast<ConstantAggregateZero>(C))
    return getConstantAggregateZero(CAZ);
  if (ConstantArray *CA = dyn_cast<ConstantArray>(C))
    return getConstantArray(CA);
  if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(C))
    return getConstantDataSequential(CDS);
  if (ConstantDataArray *CDA = dyn_cast<ConstantDataArray>(C))
    return getConstantDataArray(CDA);
  if (ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(C))
    return getConstantDataVector(CDV);
  if (ConstantFP *CFP = dyn_cast<ConstantFP>(C))
    return getConstantFP(CFP);
  if (ConstantInt *CI = dyn_cast<ConstantInt>(C))
    return getConstantInt(CI);
  if (ConstantPointerNull *CPN = dyn_cast<ConstantPointerNull>(C))
    return getConstantPointerNull(CPN, DT);
  if (ConstantStruct *CS = dyn_cast<ConstantStruct>(C))
    return getConstantStruct(CS);
  if (ConstantVector *CV = dyn_cast<ConstantVector>(C))
    return getConstantVector(CV);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C))
    return getConstantExpr(CE, DT);
  /// set as invalid if the type of constant has not found.
  if (C == PointerValue)
    return 0.0;
  setValidFalse();
}

double ConstantsSimplify::getConstantInt (const ConstantInt *C) const {
  return C->getValue().signedRoundToDouble();
}

double ConstantsSimplify::getConstantAggregateZero (
       const ConstantAggregateZero *C) const {
  return 0.0;
}

double ConstantsSimplify::getConstantArray (const ConstantArray *C) {
  // Not works with this instruciton.
  setValidFalse();
  return 0.0;
}

double ConstantsSimplify::getConstantStruct (const ConstantStruct *C) { 
  // Not works with this instruciton. 
  setValidFalse();
  return 0.0;
}

double ConstantsSimplify::getConstantDataSequential (
       const ConstantDataSequential *C) {
  // Not works with this instruciton. 
  setValidFalse();
  return 0.0;
}

double ConstantsSimplify::getConstantDataArray (
       const ConstantDataArray *C) {
  // Not works with this instruciton. 
  setValidFalse();
  return 0.0;
}

double ConstantsSimplify::getConstantDataVector (
       const ConstantDataVector *C) {
  // Not works with this instruciton. 
  setValidFalse();
  return 0.0;
}

double ConstantsSimplify::getConstantVector (
       const ConstantVector *C) {
  // Not works with this instruciton. 
  setValidFalse();
  return 0.0;
}

double ConstantsSimplify::getConstantFP (const ConstantFP *C) const {
  return C->getValueAPF().convertToDouble();
}

double ConstantsSimplify::getConstantPointerNull (ConstantPointerNull *C,
                                                 const DataLayout *DT) {
  return getSizeToType(C->getType()->getPointerElementType(), DT);
}

bool ConstantsSimplify::getConstantCmp (Constant *CI, const DataLayout *DT) {
  double value1 = 0.0, value2 = 0.0;
  bool cmpResult = false;
  
  Constant *C = cast<Constant>(CI->getOperand(0)); 
  value1 = getConstant(C, DT);
  C = cast<Constant>(CI->getOperand(1));
  value2 = getConstant(C, DT);
  
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(CI)) {
    switch (CE->getPredicate()) {
      case ICmpInst::ICMP_EQ:
        cmpResult = (value1 == value2);
      break;
      case ICmpInst::ICMP_NE:
        cmpResult = (value1 != value2);
      break;
      case ICmpInst::ICMP_UGT:
        cmpResult = (value1 > value2);
      break;
      case ICmpInst::ICMP_UGE:
        cmpResult = (value1 >= value2);
      break;
      case ICmpInst::ICMP_ULT:
        cmpResult = (value1 < value2);
      break;
      case ICmpInst::ICMP_ULE:
        cmpResult = (value1 <= value2);
      break;
      case ICmpInst::ICMP_SGT: 
        cmpResult = (value1 > value2);
      break;
      case ICmpInst::ICMP_SGE:
        cmpResult = (value1 >= value2);
      break;
      case ICmpInst::ICMP_SLT:
        cmpResult = (value1 < value2);
      break;
      case ICmpInst::ICMP_SLE:
        cmpResult = (value1 <= value2);
      break;
      default:
        setValidFalse();
      break;
    }
  } else
    setValidFalse();
  return cmpResult;
}

double ConstantsSimplify::getConstantExpr (ConstantExpr *C,
                                          const DataLayout *DT) {
  double value = 0.0;
  switch (C->getOpcode()) {
    case Instruction::Add:
        value = getConstant(C->getOperand(0), DT) 
                + getConstant(C->getOperand(1), DT);
      break;
    case Instruction::Sub:
        value = getConstant(C->getOperand(0), DT)
                - getConstant(C->getOperand(1), DT);
    break;
    case Instruction::Mul:
        value = getConstant(C->getOperand(0), DT)
                * getConstant(C->getOperand(1), DT);
    break;
    case Instruction::SDiv:
        value = getConstant(C->getOperand(0), DT)
                / getConstant(C->getOperand(1), DT);
    break;
    case Instruction::UDiv:
        value = getConstant(C->getOperand(0), DT)
                / getConstant(C->getOperand(1), DT);
    break;
    case Instruction::PtrToInt:
        value = getConstant(C->getOperand(0), DT);
    break;
    case Instruction::GetElementPtr: {
        if (getConstant(C->getOperand(0), DT) != 0) {
          setValidFalse();
          return 0.0;
        }
        double sum = 0.0;
        Type *Ty = C->getOperand(0)->getType();
        for (int i = 0, ie = C->getNumOperands(); i != ie; i++) {
          int position = getConstant(C->getOperand(i), DT);
          sum += (position * getSizeToType(Ty, DT));
          Ty = getInternalType (Ty, position, DT);
        }
        return sum;
                                     }
    break;
    case Instruction::Select: { 
         if (getConstantCmp(C->getOperand(0),DT))
           return getConstant(C->getOperand(1), DT);
         return getConstant(C->getOperand(2), DT);
                              }
    break;
    default:
         setValidFalse();
    break;
  }
  return value;
}

unsigned int ConstantsSimplify::getSizeToType (Type *tpy,                     
                                               const DataLayout *DT) {
  return ((getSizeToTypeInBits(tpy,DT) + 7) / 8);
}

unsigned int ConstantsSimplify::getSizeToTypeInBits (Type *tpy,
                                                     const DataLayout *DT) {
  switch (tpy->getTypeID()) {
    case Type::ArrayTyID :
       return DT->getTypeAllocSizeInBits(tpy);
    break;
    case Type::HalfTyID:
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::Type::FloatTyID:
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::DoubleTyID:
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::X86_FP80TyID:
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::FP128TyID:
      return tpy->getPrimitiveSizeInBits(); 
    break;
    case Type::PPC_FP128TyID:
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::X86_MMXTyID:  
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::IntegerTyID:
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::VectorTyID:
      return tpy->getPrimitiveSizeInBits();
    break;
    case Type::StructTyID: {
      StructType *ST = cast<StructType>(tpy);
      const StructLayout *SL = DT->getStructLayout(ST);
      return SL->getSizeInBits();
    }
    break;
    case Type::PointerTyID:
      return DT->getPointerTypeSizeInBits(tpy);
    break;
    default:
      // Not works to:
      //   -> Function
      //   -> Token
      //   -> Label 
      //   -> Metadata
      setValidFalse();
      return 0;
    break;
  }
}

// Return the subType to the type named "tpy", if possible.
// If not, return the same type of "tpy"
Type* ConstantsSimplify::getInternalType (Type *tpy, int position,
                                                      const DataLayout *DT) { 
  if (tpy->getTypeID() == Type::ArrayTyID)
    return tpy->getArrayElementType();
  if (tpy->getTypeID() == Type::StructTyID) {
    if (tpy->getNumContainedTypes() <= position)
      return *tpy->subtype_begin();
      return tpy->getStructElementType(position);
  }
  if (tpy->getTypeID() == Type::PointerTyID)
    return tpy->getPointerElementType();
  return tpy;
}
//===---------------------------- ConstantsSimplify.cpp -------------------===// 

