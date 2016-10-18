// Author: Fabian Gruber and Pericles Alves
//
// Region cloning utility.

#ifndef CLONE_REGION_H
#define CLONE_REGION_H

#include <llvm/Analysis/RegionInfo.h>

using namespace llvm;

namespace llvm {
class RGPassManager;
class DominatorTree;
class DominanceFrontier;
}

namespace lge {

// Clones a given region, inserting the newly created blocks in the CFG. We also
// do our best to update both the region info tree and dominance info.
Region *cloneRegion(Region *R, RGPassManager *RGM, RegionInfo *RI,
                          DominatorTree *DT, DominanceFrontier *DF);
}

#endif
