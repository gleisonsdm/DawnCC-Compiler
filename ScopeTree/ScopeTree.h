//===---------------------------- ScopeTree.h -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the Universidade Federal de Minas Gerais -
// UFMG Open Source License. See LICENSE.TXT for details.
//
// Copyright (C) 2016   Gleison Souza Diniz Mendon?a
//
//===----------------------------------------------------------------------===//
//
// Recognize and use extra debug information to generate correct insertion
// points for computation in the original source file.
// In essence, this extra information is organized in a scope tree, trying to 
// identify the correct scope dependences that are lost in the IR's
// representation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"

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

class ScopeTree : public FunctionPass {

  private:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
 
  typedef struct STnode {
    unsigned int id;
    int startLine;
    int startColumn;
    int endLine;
    int endColumn;
    std::string name;
    bool isTopLevel;
    bool isLoop;
  } STnode;
 
  typedef struct Graph {
    
    // Provides a graph to possible search.
    std::vector<std::vector<unsigned int> > nodes;
    // get the parent for each node. If is a top level region,
    // uses the same node.
    // The information is present in a tuple < child, parent >.
    std::map<unsigned int, unsigned int> parents;
    int n_nodes;
    
    // Store the name of the file, such as an id of this graph.
    std::string file;

    // Provides information to each node.
    std::map<unsigned int, STnode> list;

  } Graph;
  
  // Provides information to the Module.
  std::map<Module*, std::vector<Graph> > info;

  // Used to map functions to STnodes.
  std::map<Function*, STnode> funcNodes;

  // Used to map loops to STnodes.
  std::map<Loop*, STnode> loopNodes;

  // Provides information if is knowed the file or not.
  std::map<std::string, bool> isFileRead;
  //===---------------------------------------------------------------------===

  // Find the name of the file for instruction I.
  StringRef getFileName (Instruction *I);

  // Use debug information to identify the line of value "V" in the source code.
  int getLineNo (Value *V);

  // Initialize a STnode with default values.
  STnode initSTnode ();

  // Define if the string contains a file name.
  bool isFileName (std::string str);

  // Generate a STNode object, with the target information.
  STnode generateSTNode (std::string str);

  // Insert a node in the object "list".
  void insertNodeInList (Graph *gph, STnode node);

  // Create a new edge, using string str.
  std::pair<unsigned int, unsigned int> buildEdge (std::string str);

  // Insert an edge in a graph object.
  void insertEdge (Graph *gph, unsigned int p1, unsigned int p2);

  // Read an extern file, and use the information to built the graph.
  bool readFile (std::string name, Function *F);

  // Identify the parent for each node.
  void identifyParents (Graph *gph);

  // Validade if the statement is valid.
  bool isValidLoopStatement (STnode node, int line, int column);

  // Associate a loop with available information, case possible.
  void associateLoop (Loop *L);

  // Return the name of the function in the source file.
  std::string getFunctionNameDBG(Function *F);

  // Associate a Function with available information, case possible.
  void associateFunction (Function *F);

  // Associate extra debug information with IR representation.
  void associateIRSource (Function *F);

  // Void to show the collected information.
  void printData ();

  // Returns the loops present in a region R.
  void associateLoopstoRegion (std::map<Loop*, STnode> & Loops, Region *R);

  // Find a graph for region R.
  Graph findGraph (Region *R);

  public:

  //===---------------------------------------------------------------------===
  //                              Data Structs
  //===---------------------------------------------------------------------===
  //===---------------------------------------------------------------------===

  static char ID;

  ScopeTree() : FunctionPass(ID) { };

  // Uses loop's debug information to identify a pair <line, column> 
  // of the best place to insert computation before loops and present in this
  // unique scope.
  std::pair<unsigned int, unsigned int> getStartRegionLoops (Region *R);
  
  // Uses loop's debug information to identify a pair <line, column> 
  // of the best place to insert computation after loops and present in this
  // unique scope.
  std::pair<unsigned int, unsigned int> getEndRegionLoops (Region *R);
  
  // Identify for region R, if is safe to use extra debug information
  // of the loops in this region (in essence, if is a unique region or not.)
  bool isSafetlyRegionLoops (Region *R);

  virtual bool runOnFunction(Function &F) override;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<RegionInfoPass>();
      AU.addRequired<AliasAnalysis>();
      AU.addRequired<ScalarEvolution>();
      AU.addRequiredTransitive<LoopInfoWrapperPass>();
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.setPreservesAll();
  }

  RegionInfoPass *rp;
  AliasAnalysis *aa;
  ScalarEvolution *se;
  LoopInfo *li;
  DominatorTree *dt;

};

}

//===---------------------------- ScopeTree.h -----------------------------===//
