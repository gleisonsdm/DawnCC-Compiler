// Author: Pericles Alves [periclesrafael@dcc.ufmg.br]
//
// Utility for computing symbolic bounds for Scalar Evolution expressions in a
// given program point. For the following code:
//
//   for (int i = 0; i < n; i++)
//     a[i] = i;
//
// The following range computation instructions would be inserted (in this case,
// at the loop pre-header):
//
//   // Symbolic limit("a[i]") : (a+0, a+n-1)
//   lower_a_i = 0;
//   upper_a_i = (a+n-1);
//   for (int i = 0; i < n; i++)
//     a[i] = i;
//
// This utility also has an analysis mode, where we only check if symbolic
// ranges CAN be computed for a given Scalar Evolution expression at a given
// program point, but don't actually insert range computation instructions in
// the CFG.

#ifndef SCEV_RANGE_BUILDER_H
#define SCEV_RANGE_BUILDER_H 1

#define DUMMY_VAL ((Value *)0x1)

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolutionExpander.h>
#include <map>
#include <set>

using namespace llvm;

namespace llvm {
class AliasAnalysis;
class Region;
}

namespace lge {

class SCEVRangeBuilder : private SCEVExpander {
  friend class AliasInstrumentation;

  ScalarEvolution *SE;
  AliasAnalysis *AA;
  LoopInfo *LI;
  DominatorTree *DT;
  Region *R;
  const DataLayout &DL;
  bool CurrentUpper; // Which bound is currently being extracted. Used mainly
                     // by methods of SCEVExpander, which are not aware of
                     // bounds computation.
  bool AnalysisMode; // When set, instructions are not inserted in the CFG.
                     // Every function that generates instructions simply return
                     // a dummy not-null value.
  std::map<std::tuple<const SCEV *, Instruction *, bool>, TrackingVH<Value>>
      InsertedExpressions; // Saved expressions for reuse.
  std::map<const Loop *, const SCEV *> ArtificialBECounts; // Holds artificially
                                                           // created back-edge
                                                           // counts for loops.

  void setAnalysisMode(bool Val) { AnalysisMode = Val; }

  void setArtificialBECounts(
      std::map<const Loop *, const SCEV *> ArtificialBECounts) {
    this->ArtificialBECounts = ArtificialBECounts;
  }

  // If the caller doesn't specify which bound to compute, we assume the same of
  // the last expanded expression. Usually called by methods defined in
  // SCEVExpander.
  Value *expand(const SCEV *S) { return expand(S, CurrentUpper); }

  // Main entry point for expansion.
  Value *expand(const SCEV *S, bool Upper);

  Value *getSavedExpression(const SCEV *S, Instruction *InsertPt, bool Upper);
  void rememberExpression(const SCEV *S, Instruction *InsertPt, bool Upper,
                          Value *V);

  // We need to overwrite this method so the most specialized visit methods are
  // called before the visitors on SCEVExpander.
  Value *visit(const SCEV *S, bool Upper) {
    switch (S->getSCEVType()) {
    case scConstant:
      return visitConstant((const SCEVConstant *)S, Upper);
    case scTruncate:
      return visitTruncateExpr((const SCEVTruncateExpr *)S, Upper);
    case scZeroExtend:
      return visitZeroExtendExpr((const SCEVZeroExtendExpr *)S, Upper);
    case scSignExtend:
      return visitSignExtendExpr((const SCEVSignExtendExpr *)S, Upper);
    case scAddExpr:
      return visitAddExpr((const SCEVAddExpr *)S, Upper);
    case scMulExpr:
      return visitMulExpr((const SCEVMulExpr *)S, Upper);
    case scUDivExpr:
      return visitUDivExpr((const SCEVUDivExpr *)S, Upper);
    case scAddRecExpr:
      return visitAddRecExpr((const SCEVAddRecExpr *)S, Upper);
    case scSMaxExpr:
      return visitSMaxExpr((const SCEVSMaxExpr *)S, Upper);
    case scUMaxExpr:
      return visitUMaxExpr((const SCEVUMaxExpr *)S, Upper);
    case scUnknown:
      return visitUnknown((const SCEVUnknown *)S, Upper);
    case scCouldNotCompute:
      return nullptr;
    default:
      llvm_unreachable("Unknown SCEV type!");
    }
  }

  // Find detailed description for each method at their implementation headers.
  Value *visitConstant(const SCEVConstant *Constant, bool Upper);
  Value *visitTruncateExpr(const SCEVTruncateExpr *Expr, bool Upper);
  Value *visitZeroExtendExpr(const SCEVZeroExtendExpr *Expr, bool Upper);
  Value *visitSignExtendExpr(const SCEVSignExtendExpr *Expr, bool Upper);
  Value *visitAddExpr(const SCEVAddExpr *Expr, bool Upper);
  Value *visitMulExpr(const SCEVMulExpr *Expr, bool Upper);
  Value *visitUDivExpr(const SCEVUDivExpr *Expr, bool Upper);
  Value *visitAddRecExpr(const SCEVAddRecExpr *Expr, bool Upper);
  Value *visitUMaxExpr(const SCEVUMaxExpr *Expr, bool Upper);
  Value *visitSMaxExpr(const SCEVSMaxExpr *Expr, bool Upper);
  Value *visitUnknown(const SCEVUnknown *Expr, bool Upper);

  // Generates code for an expression by using the SCEVExpander infrastructure.
  // When this method is called, the bounds for each operand of the expression
  // MUST be already available in the expression cache.
  Value *generateCodeThroughExpander(const SCEV *Expr);

  // Interceptors for SCEVExpander methods, so we can avoid actual instruction
  // generation during analysis mode.
  Value *InsertBinop(Instruction::BinaryOps Op, Value *Lhs, Value *Rhs);
  Value *InsertCast(Instruction::CastOps Op, Value *V, Type *SestTy);
  Value *InsertICmp(CmpInst::Predicate P, Value *Lhs, Value *Rhs);
  Value *InsertSelect(Value *V, Value *T, Value *F, const Twine &Name = "");
  Value *InsertNoopCastOfTo(Value *V, Type *Ty);

  // Generates the lower or upper bound for a set of unsigned expressions. More
  // details in the method implementation header.
  Value *getULowerOrUpperBound(const std::vector<const SCEV *> &ExprList,
                               bool Upper);

public:
  SCEVRangeBuilder(ScalarEvolution *SE, const DataLayout &DL, AliasAnalysis *AA,
      LoopInfo *LI, DominatorTree *DT, Region *R, Instruction *InsertPtr)
      : SCEVExpander(*SE, DL, "scevrange"), SE(SE), AA(AA), LI(LI), DT(DT),
        R(R), DL(DL), CurrentUpper(true), AnalysisMode(false) {
    SetInsertPoint(InsertPtr);
  }

  // Returns the minimum value an SCEV can assume.
  Value *getLowerBound(const SCEV *S) { return expand(S, /*Upper*/ false); }

  // Returns the maximum value an SCEV can assume.
  Value *getUpperBound(const SCEV *S) { return expand(S, /*Upper*/ true); }

  // Generate the smallest lower bound and greatest upper bound for a set of
  // expressions. All expressions are assumed to be type consistent (all of the
  // same type) and produce an unsigned result.
  Value *getULowerBound(const std::vector<const SCEV *> &ExprList);
  Value *getUUpperBound(const std::vector<const SCEV *> &ExprList);

  // Verify if bounds can be generated for a single SCEV (or a list of them)
  // without actually inserting bounds computation instructions.
  bool canComputeBoundsFor(const SCEV *Expr);
  bool canComputeBoundsFor(const std::set<const SCEV *> &ExprList);

  // Add the element size to the upper bound of a base pointer, so the new upper
  // bound will be the first byte after the pointed memory region.
  Value *stretchPtrUpperBound(Value *BasePtr, Value *UpperBound);
};
} // end lge namespace

#endif // SCEV_RANGE_BUILDER_H
