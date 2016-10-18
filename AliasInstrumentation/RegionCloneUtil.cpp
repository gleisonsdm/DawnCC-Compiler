// Author: Fabian Gruber and Pericles Alves

#include "RegionCloneUtil.h"

#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/IR/IRBuilder.h>
#include <set>

using namespace llvm;

typedef std::set<Value*> ValueSet;
typedef std::set<Instruction*> InstrSet;

// Finds values created within a given region and used outside of it.
static void findOutputs(Region *region, InstrSet& Outputs) {
	for (auto BB : region->blocks())
		for (auto II = BB->begin(), IE = BB->end(); II != IE; ++II)
			for (User *user : II->users())
				if (!region->contains(cast<Instruction>(user)))
					Outputs.insert(II);
}

// Clones the dominator tree and dominance info of a given basic block. The
// received block is expected to be already cloned.
static void cloneDominatorInfo(BasicBlock *BB,
                               ValueMap<const Value*, WeakVH> &VMap,
                               DominatorTree *DT, DominanceFrontier *DF) {
  auto BI = VMap.find(BB);
  auto NewBB = cast<BasicBlock>(BI->second);

  // NewBB already got dominator info.
  if (DT->getNode(NewBB))
    return;

  BasicBlock *BBDom = DT->getNode(BB)->getIDom()->getBlock();

  // NewBB's dominator is either BB's dominator or BB's dominator's clone.
  BasicBlock *NewBBDom = BBDom;
  auto BBDomI = VMap.find(BBDom);

  if (BBDomI != VMap.end()) {
    NewBBDom = cast<BasicBlock>(BBDomI->second);
    if (!DT->getNode(NewBBDom)) cloneDominatorInfo(BBDom, VMap, DT, DF);
  }

  DT->addNewBlock(NewBB, NewBBDom);

  // Copy cloned dominance frontiner set
  if (DF) {
    DominanceFrontier::DomSetType NewDFSet;
    auto DFI = DF->find(BB);

    if ( DFI != DF->end()) {
      DominanceFrontier::DomSetType S = DFI->second;

      for (auto I = S.begin(), E = S.end(); I != E; ++I) {
        BasicBlock *DB = *I;
        auto IDM = VMap.find(DB);

        if (IDM != VMap.end())
          NewDFSet.insert(cast<BasicBlock>(IDM->second));
        else
          NewDFSet.insert(DB);
      }
    }

    DF->addBasicBlock(NewBB, NewDFSet);
  }
}

Region *lge::cloneRegion(Region *R, RGPassManager *RGM, RegionInfo *RI,
                         DominatorTree *DT, DominanceFrontier *DF) {
  ValueMap<const Value*, WeakVH> VMap;
  SmallVector<BasicBlock *, 16> NewBlocks;
  InstrSet Outputs;

  findOutputs(R, Outputs);

  // Clone all basic blocks in the region.
  for (auto I = R->block_begin(), E = R->block_end(); I != E; ++I) {
    BasicBlock *BB = *I;
    BasicBlock *NewBB = CloneBasicBlock(BB, VMap, ".clone");
    VMap[BB] = NewBB;
    NewBlocks.push_back(NewBB);
  }

  // Clone dominator info.
  if (DT) {
    for (auto I = R->block_begin(), E = R->block_end(); I != E; ++I) {
      BasicBlock *BB = *I;
      cloneDominatorInfo(BB, VMap, DT, DF);
    }
  }

  // Remap instructions to reference operands from VMap.
  for (auto NBI = NewBlocks.begin(), NBE = NewBlocks.end();  NBI != NBE; ++NBI) {
    BasicBlock *NB = *NBI;

    for (auto BI = NB->begin(), BE = NB->end(); BI != BE; ++BI) {
      Instruction *Inst = BI;

      for (unsigned Idx = 0, num_ops = Inst->getNumOperands(); Idx != num_ops;
           ++Idx) {
        Value *Op = Inst->getOperand(Idx);
        auto OpItr = VMap.find(Op);

        if (OpItr != VMap.end())
          Inst->setOperand(Idx, OpItr->second);

        // Fix phi-functions.
        if(Inst->getOpcode() == Instruction::PHI) {
          PHINode *phiNode = (PHINode *)Inst;
          auto I = VMap.find(phiNode->getIncomingBlock(Idx));

          if (I != VMap.end())
            phiNode->setIncomingBlock(Idx, cast<BasicBlock>(I->second));
        }
      }
    }
  }

  Function *F = R->getEnteringBlock()->getParent();
  F->getBasicBlockList().insert(R->getEntry(), NewBlocks.begin(),
                                NewBlocks.end());

  auto newRegion = new
    Region(cast<BasicBlock>(VMap.find(R->getEntry())->second), R->getExit(), RI,
           DT, R->getParent());

  // Alter phis in the exit block of the region.

  auto Exiting = R->getExitingBlock(); // Inside the region.
  auto Exit = R->getExit(); // Outside the region

  std::set<PHINode*> UpdatedPHIs;

	// Add cloned basic blocks to phis that use blocks of the original region.
	for (auto BB : R->blocks())	{
		auto terminator = BB->getTerminator();

		for (unsigned i = 0, end = terminator->getNumSuccessors(); i < end; i++) {
			auto succ = terminator->getSuccessor(i);

			// Only update phis outside the region.
			if (R->contains(succ))
				continue;

			for (auto It = succ->begin(), end = succ->end(); It != end; ++It)	{
				PHINode *PN = dyn_cast<PHINode>(It);

				if (!PN)
					break;

				// If the phi uses the original block we must add the cloned version.
				int Idx = PN->getBasicBlockIndex(BB);

				if (Idx < 0)
					continue;

				auto Val = PN->getIncomingValue(Idx);

				// If value comes from the original region, try to find the cloned
        // value. Otherwise use the value itself.
				auto ClonedEntry = VMap.find(Val);

				Value *ClonedVal = (ClonedEntry != VMap.end())
            ? ((Value*) ClonedEntry->second) : Val;

				PN->addIncoming(ClonedVal, cast<BasicBlock>(VMap[BB]));
        UpdatedPHIs.insert(PN);
			}
		}
	}

	// Replace uses of values produced inside the original region with phis that
  // also receive the value from the cloned region.

	BasicBlock *ClonedExiting = [&]() {
		auto it = VMap.find(Exiting);
		return cast<BasicBlock>(it->second);
	}();

	IRBuilder<> irb{Exit->begin()};

	for (Value *Output : Outputs)	{
    InstrSet userSet;

    // Collect all uses outside the region.
		for (User *user : Output->users()) {
			// an instruction can only be used by instructions, right?
			auto *using_instr = cast<Instruction>(user);

			if (R->contains(using_instr))
				continue;

      // We already update PHIs that use values from within the region.
      if (isa<PHINode>(using_instr) &&
          UpdatedPHIs.count(dyn_cast<PHINode>(using_instr)))
        continue;

      userSet.insert(using_instr);
		}

    // Only insert a phi if we really need it.
    if (userSet.empty())
      continue;

		PHINode *phi = irb.CreatePHI(Output->getType(), 2);

    // Replace all uses of output with the newly created phi
    for (Instruction *user : userSet)
      user->replaceUsesOfWith(Output, phi);

		// can't add these before, otherwise use in new phi would be replaced
		phi->addIncoming(Output, Exiting);
		phi->addIncoming(VMap[Output], ClonedExiting);
	}

  return newRegion;
}
