// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]

#include "SCEVRangeBuilder.h"
#include "PtrRangeAnalysis.h"

using namespace llvm;
using namespace lge;

Value *SCEVRangeBuilder::getSavedExpression(const SCEV *S,
                                            Instruction *InsertPt, bool Upper) {
  auto I = InsertedExpressions.find(std::make_tuple(S, InsertPt, Upper));

  if (I != InsertedExpressions.end())
    return I->second;

  return NULL;
}

void SCEVRangeBuilder::rememberExpression(const SCEV *S, Instruction *InsertPt,
                                          bool Upper, Value *V) {
  InsertedExpressions[std::make_tuple(S, InsertPt, Upper)] = V;
}

Value *SCEVRangeBuilder::expand(const SCEV *S, bool Upper) {
  // Check expression cache before expansion.
  Instruction *InsertPt = getInsertPoint();
  Value *V = getSavedExpression(S, InsertPt, Upper);
  
  if (V)
    return V;

  // Remember which bound was computed for the last expression.
  bool OldUpper = CurrentUpper;

  CurrentUpper = Upper;
  V = visit(S, Upper);

  // In analysis mode, V is just a dummy value.
  if (!AnalysisMode)
    rememberExpression(S, InsertPt, Upper, V);

  CurrentUpper = OldUpper;

  return V;
}

Value *SCEVRangeBuilder::visitConstant(const SCEVConstant *Constant,
                                       bool Upper) {
  return Constant->getValue();
}

// If the original value is within an overflow-free range, we simply return
// the truncated bound. If not, we define the bound to be the maximum/minimum
// value the destination bitwidth can assume. The overflow-free range is
// defined as the greatest lower bound and least upper pound among the types
// that the destination bitwidth can represent.
Value *SCEVRangeBuilder::visitTruncateExpr(const SCEVTruncateExpr *Expr,
                                           bool Upper) {
  Type *DstTy = SE->getEffectiveSCEVType(Expr->getType());
  Type *SrcTy = SE->getEffectiveSCEVType(Expr->getOperand()->getType());
  Value *Bound = expand(Expr->getOperand(), Upper);

  if (!Bound)
    return nullptr;

  Bound = InsertNoopCastOfTo(Bound, SrcTy);

  // Maximum/minimum value guaranteed to be overflow-free after trunc and
  // maximum/minimum value the destination type can assume.
  unsigned DstBW = DstTy->getIntegerBitWidth();
  const APInt &APnoOFLimit =
      (Upper ? APInt::getSignedMaxValue(DstBW) : APInt::getMinValue(DstBW));
  const APInt &APTyLimit =
      (Upper ? APInt::getMaxValue(DstBW) : APInt::getSignedMinValue(DstBW));

  // Build actual bound selection.
  Value *NoOFLimit = InsertCast(Instruction::SExt,
                                ConstantInt::get(DstTy, APnoOFLimit), SrcTy);
  Value *TyLimit =
      InsertCast(Instruction::SExt, ConstantInt::get(DstTy, APTyLimit), SrcTy);
  Value *Icmp = (Upper ? InsertICmp(ICmpInst::ICMP_SGT, Bound, NoOFLimit)
                       : InsertICmp(ICmpInst::ICMP_SLT, Bound, NoOFLimit));
  Value *Sel = InsertSelect(Icmp, TyLimit, Bound, "sbound");
  Value *Inst = InsertCast(Instruction::Trunc, Sel, DstTy);
  
  return Inst;
}

// Expand and save the bound of the operand on the expression cache, then
// invloke the expander visitor to generate the actual code.
// - upper_bound: zero_extend (upper_bound(op))
// - lower_bound: zero_extend (lower_bound(op))
Value *SCEVRangeBuilder::visitZeroExtendExpr(const SCEVZeroExtendExpr *Expr,
                                             bool Upper) {
  if (!expand(Expr->getOperand()))
    return nullptr;

  return generateCodeThroughExpander(Expr);
}

// Expand operands here first, to check the existence of their bounds, then
// call the expander visitor to generate the actual code.
// - upper_bound: sext(upper_bound(op))
// - lower_bound: sext(lower_bound(op))
Value *SCEVRangeBuilder::visitSignExtendExpr(const SCEVSignExtendExpr *Expr,
                                             bool Upper) {
  if (!expand(Expr->getOperand()))
    return nullptr;

  return generateCodeThroughExpander(Expr);
}

// Simply put all operands on the expression cache and let the expander insert
// the actual instructions.
// - upper_bound: upper_bound(op) + upper_bound(op)
// - lower_bound: lower_bound(op) + lower_bound(op)
Value *SCEVRangeBuilder::visitAddExpr(const SCEVAddExpr *Expr, bool Upper) {
  for (unsigned I = 0, E = Expr->getNumOperands(); I < E; ++I) {
    const SCEV *Op = Expr->getOperand(I);

    // Invert the sign of negative operands.
    if (Op->isNonConstantNegative())
      Op = SE->getNegativeSCEV(Op);

    if (!expand(Op))
      return nullptr;
  }

  return generateCodeThroughExpander(Expr);
}

// We only handle the case where one of the operands is a constant (%v1 * %v2).
// if one operand is a constant, try to solve the expression depends of the
// constant's signal.
// - if C >= 0:
//   . upper_bound: C * upper_bound(op2)
//   . lower_bound: C * lower_bound(op2)
// - if C < 0:
//   . upper_bound: C * lower_bound(op2)
//   . lower_bound: C * upper_bound(op2)
Value *SCEVRangeBuilder::visitMulExpr(const SCEVMulExpr *Expr, bool Upper) {
  if (Expr->getNumOperands() != 2) {
    return nullptr;
  }
  // If there is a constant, it will be the first operand.
  const SCEVConstant *SC1 = dyn_cast<SCEVConstant>(Expr->getOperand(0));
  const SCEVConstant *SC2 = dyn_cast<SCEVConstant>(Expr->getOperand(1));
  Type *Ty = SE->getEffectiveSCEVType(Expr->getType());
  Value *Lhs = nullptr, *Rhs = nullptr;
  bool InvertBounds1 = false, InvertBounds2 = false;


  if (SC1)
    InvertBounds1 = SC1->getValue()->getValue().isNegative();

  if (SC2)
    InvertBounds2 = SC2->getValue()->getValue().isNegative(); 
    
  if (SC1 && SC2) {
    Value *SCCast1 = InsertNoopCastOfTo(SC1->getValue(), Ty);
    Value *SCCast2 = InsertNoopCastOfTo(SC1->getValue(), Ty);
    return InsertBinop(Instruction::Mul, SCCast1, SCCast2);
  }

  if (!SC2 && SC1) {
    Rhs = expand(Expr->getOperand(1), InvertBounds1 ? !Upper : Upper);
    if (!Rhs)
      return nullptr;

    Rhs = InsertNoopCastOfTo(Rhs, Ty);
    Value *SCCast = InsertNoopCastOfTo(SC1->getValue(), Ty);
    return InsertBinop(Instruction::Mul, SCCast, Rhs);
  }

  if (!SC1 && SC2) {
    Lhs = expand(Expr->getOperand(0), InvertBounds2 ? !Upper : Upper);
    if (!Lhs)
      return nullptr;
    Lhs = InsertNoopCastOfTo(Lhs, Ty);
    Value *SCCast = InsertNoopCastOfTo(SC2->getValue(), Ty);
    return InsertBinop(Instruction::Mul, Lhs, SCCast);
  }
  
  if (!SC1 && !SC2) {
    Lhs = expand(Expr->getOperand(0), Upper);
    Rhs = expand(Expr->getOperand(1), Upper);
    if (!Lhs || !Rhs)
      return nullptr;

    Lhs = InsertNoopCastOfTo(Lhs, Ty);
    Rhs = InsertNoopCastOfTo(Rhs, Ty);
    return InsertBinop(Instruction::Mul, Lhs, Rhs);
  }

  return nullptr;
}

// This code is based on the visitUDiv code from SCEVExpander. We only
// reproduce it here because it involves using mixed bounds to compute a
// single bound.
// - upper_bound: upper_bound(lhs) / lower_bound(rhs)
// - lower_bound: lower_bound(lhs) / upper_bound(rhs)
Value *SCEVRangeBuilder::visitUDivExpr(const SCEVUDivExpr *Expr, bool Upper) {
  Type *Ty = SE->getEffectiveSCEVType(Expr->getType());
  Value *Lhs = expand(Expr->getLHS(), Upper);

  if (!Lhs)
    return nullptr;

  Lhs = InsertNoopCastOfTo(Lhs, Ty);

  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Expr->getRHS())) {
    const APInt &Rhs = SC->getValue()->getValue();
    if (Rhs.isPowerOf2())
      return InsertBinop(Instruction::LShr, Lhs,
                         ConstantInt::get(Ty, Rhs.logBase2()));
  }

  Value *Rhs = expand(Expr->getRHS(), !Upper);

  if (!Rhs)
    return nullptr;

  Rhs = InsertNoopCastOfTo(Rhs, Ty);

  return InsertBinop(Instruction::UDiv, Lhs, Rhs);
}

// Compute bounds for an expression of the type {%start, +, %step}<%loop>.
// - upper: upper(%start) + upper(%step) * upper(backedge_taken(%loop))
// - lower_bound: lower_bound(%start)
Value *SCEVRangeBuilder::visitAddRecExpr(const SCEVAddRecExpr *Expr,
                                         bool Upper) {
  // If the access expression is quadratic, we need to invalidate our results.
  // We can have expressions as:
  //   for (i = 0; i < n; i++) {
  //     v[k] = i;
  //     k += i;
  //   }
  // In this case, the result expanding is ((n - 1) * (n - 1))
  // But the correct result is (((n-1) * n) / 2)
  if (Expr->isQuadratic())
    return nullptr;
 
  // lower.
  if (!Upper)
    return expand(Expr->getStart(), /*Upper*/ false);

  // upper.
  // Cast all values to the effective start value type.
  Type *OpTy = SE->getEffectiveSCEVType(Expr->getStart()->getType());
  const SCEV *StartSCEV = SE->getTruncateOrSignExtend(Expr->getStart(), OpTy);
  const SCEV *StepSCEV =
      SE->getTruncateOrSignExtend(Expr->getStepRecurrence(*SE), OpTy);
  const SCEV *BEdgeCountSCEV;
  const Loop *L = Expr->getLoop();

  // Try to compute a symbolic limit for the loop iterations, so we can fix a
  // bound for a recurrence over it. If a BE count limit is not available for
  // the loop, check if an artificial limit was provided for it.
  if (SE->hasLoopInvariantBackedgeTakenCount(L))
    BEdgeCountSCEV = SE->getBackedgeTakenCount(L);
  else if (ArtificialBECounts.count(L))
    BEdgeCountSCEV = ArtificialBECounts[L];
  else
    return nullptr;

  BEdgeCountSCEV = SE->getTruncateOrSignExtend(BEdgeCountSCEV, OpTy);
  Value *Start = expand(StartSCEV, Upper);
  Value *Step = expand(StepSCEV, Upper);
  Value *BEdgeCount = expand(BEdgeCountSCEV, Upper);

  if (!Start || !Step || !BEdgeCount)
    return nullptr;

  // Build the actual computation.
  Start = InsertNoopCastOfTo(Start, OpTy);
  Step = InsertNoopCastOfTo(Step, OpTy);
  BEdgeCount = InsertNoopCastOfTo(BEdgeCount, OpTy);
  Value *Mul = InsertBinop(Instruction::Mul, Step, BEdgeCount);
  Value *Bound = InsertBinop(Instruction::Add, Start, Mul);

  // From this point on, we already know this bound can be computed.
  if (AnalysisMode)
    return DUMMY_VAL;

  // Convert the result back to the original type if needed.
  Type *Ty = SE->getEffectiveSCEVType(Expr->getType());
  const SCEV *BoundCast =
      SE->getTruncateOrSignExtend(SE->getUnknown(Bound), Ty);
  return expand(BoundCast, Upper);
}

// Simply expand all operands and save them on the expression cache. We then
// call the base expander to build the final expression. This is done so we
// can check that all operands have computable bounds before we build the
// actual instructions.
// - upper_bound: umax(upper_bound(op_1), ... upper_bound(op_N))
// - lower_bound: umax(lower_bound(op_1), ... lower_bound(op_N))
Value *SCEVRangeBuilder::visitUMaxExpr(const SCEVUMaxExpr *Expr, bool Upper) {
  for (unsigned I = 0, E = Expr->getNumOperands(); I < E; ++I) {
    if (!expand(Expr->getOperand(I)))
      return nullptr;
  }

  return generateCodeThroughExpander(Expr);
}

// Simply expand all operands and save them on the expression cache. We then
// call the base expander to build the final expression. This is done so we
// can check that all operands have computable bounds before we build the
// actual instructions.
// - upper_bound: max(upper_bound(op_1), ... upper_bound(op_N))
// - lower_bound: max(lower_bound(op_1), ... lower_bound(op_N))
Value *SCEVRangeBuilder::visitSMaxExpr(const SCEVSMaxExpr *Expr, bool Upper) {
  for (unsigned I = 0, E = Expr->getNumOperands(); I < E; ++I) {
    if (!expand(Expr->getOperand(I)))
      return nullptr;
  }

  return generateCodeThroughExpander(Expr);
}

// Reduce in one unit the value of V.
// Example:
//  V = 100
//  V = 100 - 1
//  ==>> V = 99
Value *SCEVRangeBuilder::reduceOne(Value *V) {
  long long int trash = 0x1;
  if (!V || V == DUMMY_VAL)
    return V;
  Type *Ty = V->getType();
  APInt AI = APInt(32, -1, true);
  Value *Val;
  Val = Constant::getIntegerValue(Type::getInt32Ty(Ty->getContext()), AI);
  return InsertBinop(Instruction::Add, V, Val); 
}

// Try visit a srem instruction. If possible, return the operand that limit
// the bound of access. 
// Example:
// - i % 1000
// in this case, return 1000 to upper bound, and 0 to lower bound.
Value *SCEVRangeBuilder::visitSRemInst(const SCEVUnknown *Expr, bool Upper) {
  Value *Val = Expr->getValue();
  Instruction *Inst = dyn_cast<Instruction>(Val);
  
  if (Inst->getOpcode() != Instruction::SRem || Inst->getNumOperands() != 2)
    return nullptr;
  Value *V = Inst->getOperand(1);
  
  // Just return the value if it is invariant.
  if (!isInvariant(V, R, LI, AA))
    return nullptr;

  // If is not an instruction of interest, return nullptr.
  if (!isa<Constant>(V) && !isa<GlobalValue>(V) && !isa<Argument>(V) &&
      !isa<AllocaInst>(V) && !isa<LoadInst>(V) && !isa<GetElementPtrInst>(V))
  return nullptr;
  Type *Ty = V->getType();
  
  if (!Upper)
    V = Constant::getNullValue(Type::getInt32Ty(Ty->getContext()));
  return V;

}

// The bounds of a generic value are the value itself.
Value *SCEVRangeBuilder::visitUnknown(const SCEVUnknown *Expr, bool Upper) {
  Value *Val = Expr->getValue();
  Instruction *Inst = dyn_cast<Instruction>(Val);
  BasicBlock::iterator InsertPt = getInsertPoint();

  // The value must be a region parameter.
  if (!isInvariant(Val, R, LI, AA))
    return visitSRemInst(Expr, Upper);
  
  // To be used in range computation, the instruction must be available at the
  // insertion point.
  if (Inst && !DT->dominates(Inst, InsertPt))
    return visitSRemInst(Expr, Upper);

  return Val;
}

Value *SCEVRangeBuilder::generateCodeThroughExpander(const SCEV *Expr) {
  return AnalysisMode ? DUMMY_VAL : SCEVExpander::visit(Expr);
}

Value *SCEVRangeBuilder::InsertBinop(Instruction::BinaryOps Op, Value *Lhs,
                                     Value *Rhs) {
  return AnalysisMode ? DUMMY_VAL : SCEVExpander::InsertBinop(Op, Lhs, Rhs);
}

Value *SCEVRangeBuilder::InsertCast(Instruction::CastOps Op, Value *V,
                                    Type *DestTy) {
  return AnalysisMode ? DUMMY_VAL : SCEVExpander::InsertCast(Op, V, DestTy);
}

Value *SCEVRangeBuilder::InsertICmp(CmpInst::Predicate P, Value *Lhs,
                                    Value *Rhs) {
  return AnalysisMode ? DUMMY_VAL : SCEVExpander::InsertICmp(P, Lhs, Rhs);
}

Value *SCEVRangeBuilder::InsertSelect(Value *V, Value *T, Value *F,
                                      const Twine &Name) {
  return AnalysisMode ? DUMMY_VAL : SCEVExpander::InsertSelect(V, T, F, Name);
}

Value *SCEVRangeBuilder::InsertNoopCastOfTo(Value *V, Type *Ty) {
  if (AnalysisMode)
    return DUMMY_VAL;

  Value *Result = SCEVExpander::InsertNoopCastOfTo(V, Ty);

  // Enforce the resulting type if SCEVExpander fails to do so.
  if (Result->getType() != Ty) {
    Instruction::CastOps Op = CastInst::getCastOpcode(Result, false, Ty, false);
    Result =
        CastInst::Create(Op, Result, Ty, Result->getName(), getInsertPoint());
  }

  return Result;
}

// Generates the final bound by building a chain of either UMin or UMax
// operations on the bounds of each expression in the list.
// - lower_bound: umin(exprN, umin(exprN-1, ... umin(expr2, expr1)))
// - upper_bound: umax(exprN, umax(exprN-1, ... umax(expr2, expr1)))
Value *SCEVRangeBuilder::getULowerOrUpperBound(
    const std::vector<const SCEV *> &ExprList, bool Upper) {
  if (ExprList.size() < 1)
    return nullptr;

  auto It = ExprList.begin();
  const SCEV *Expr = *It;
  Value *BestBound = expand(Expr, Upper);
  ++It;

  if (!BestBound)
    return nullptr;

  while (It != ExprList.end()) {
    Expr = *It;
    Value *NewBound = expand(Expr, Upper);
    Value *Cmp;

    if (!NewBound)
      return nullptr;

    // The old bound is promoted on type conflicts.
    if (BestBound->getType() != NewBound->getType())
      BestBound = InsertNoopCastOfTo(BestBound, NewBound->getType());

    if (Upper)
      Cmp = InsertICmp(ICmpInst::ICMP_UGT, NewBound, BestBound);
    else
      Cmp = InsertICmp(ICmpInst::ICMP_ULT, NewBound, BestBound);

    BestBound =
        InsertSelect(Cmp, NewBound, BestBound, (Upper ? "umax" : "umin"));
    ++It;
  }
  return BestBound;
}

Value *
SCEVRangeBuilder::getULowerBound(const std::vector<const SCEV *> &ExprList) {
  return getULowerOrUpperBound(ExprList, /*Upper*/ false);
}

Value *
SCEVRangeBuilder::getUUpperBound(const std::vector<const SCEV *> &ExprList) {
  return getULowerOrUpperBound(ExprList, /*Upper*/ true);
}

bool SCEVRangeBuilder::canComputeBoundsFor(const SCEV *Expr) {
  // Avoid instruction insertion.
  setAnalysisMode(true);

  // Try to compute both bounds for the expression.
  bool CanComputeBounds =
      expand(Expr, /*Upper*/ false) && expand(Expr, /*Upper*/ true);

  setAnalysisMode(false);
  
  return CanComputeBounds;
}

bool
SCEVRangeBuilder::canComputeBoundsFor(const std::set<const SCEV *> &ExprList) {
  for (auto Expr : ExprList)
    if (!canComputeBoundsFor(Expr))
      return false;

  return true;
}

Value *SCEVRangeBuilder::stretchPtrUpperBound(Value *BasePtr,
                                              Value *UpperBound) {
  Type *BoundTy = UpperBound->getType();

  // We can only perform arithmetic operations on integers types.
  if (!BoundTy->isIntegerTy()) {
    BoundTy = DL.getIntPtrType(BoundTy);
    UpperBound = InsertNoopCastOfTo(UpperBound, BoundTy);
  }

  // As the base pointer might be multi-dimensional, we extract its innermost
  // element type.
  Type *ElemTy = BasePtr->getType();

  while (isa<SequentialType>(ElemTy))
    ElemTy = cast<SequentialType>(ElemTy)->getElementType();

  Constant *ElemSize =
      ConstantInt::get(BoundTy, DL.getTypeAllocSize(ElemTy));
  return InsertBinop(Instruction::Add, UpperBound, ElemSize);
}
