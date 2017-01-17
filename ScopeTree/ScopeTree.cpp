//===---------------------------- ScopeTree.cpp --------------------------===//
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

#include <fstream>
#include <queue>
#include <iostream>

#include "llvm/Analysis/RegionInfo.h"  
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DIBuilder.h" 
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/Statistic.h"

#include "ScopeTree.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "scopeTree"

#define DEFVAL 999999

StringRef ScopeTree::getFileName(Instruction *I) {
  MDNode *Var = I->getMetadata("dbg");
  if (Var)
    if (DILocation *DL = dyn_cast<DILocation>(Var))
      return DL->getFilename();
  return std::string();
}

int ScopeTree::getLineNo(Value *V) {
  if(!V)
    return -1;
  if (Instruction *I = dyn_cast<Instruction>(V))
      if (MDNode *N = I->getMetadata("dbg"))
        if (DILocation *DL = dyn_cast<DILocation>(N))
          return DL->getLine();
  return -1;
}

ScopeTree::STnode ScopeTree::initSTnode () {
  STnode node;
  node.id = 0;
  node.startLine = 0;
  node.startColumn = 0;
  node.endLine = 0;
  node.endColumn = 0;
  node.name = std::string();
  node.isLoop = false;
  node.isTopLevel = false;
  return node;
}

bool ScopeTree::isFileName (std::string str) {
  std::string subStr = "File: ";
  for (unsigned int i = 0, ie = 6; i != ie; i++)
    if (str[i] != subStr[i])
      return false;
  return true;
}

ScopeTree::STnode ScopeTree::generateSTNode (std::string str) {
  STnode node = initSTnode();
  unsigned int i = 0, ie = str.size();
  
  for (;(i != ie) && (str[i] != ' '); i++)
    node.id = ((node.id * 10) + (str[i] - '0'));

  std::string subStr = "label=\"";
  for (;i != ie; i++) {
    bool match = true;
    for (unsigned int j = 0, je = 7; (j < je) && ((i + j) < ie); j++) {
      if (str[i+j] != subStr[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      i = i + 7;
      break;
    }
  }
  
  if (i == ie)
    return node;

  subStr = std::string();
  for (;(i != ie) && (str[i] != '\"'); i++)
    subStr += str[i];
  
  // Case subStr has the fisrt simbol as \" , we identify a top level node.
  if (isFileName(subStr)) {
    for (unsigned int j = 6, je = subStr.size(); j != je; j++) {
      node.name += subStr[j];
    }
    node.isTopLevel = true;
    return node;
  }

  // Restart the processament:
  i = 0;
  ie = subStr.size();
  node.isTopLevel = false;

  for (;(i != ie) && (subStr[i] != '\\'); i++)
    node.name += subStr[i];

  for (i += 3;(i != ie) && (subStr[i] != ':'); i++)
    node.startLine = ((node.startLine * 10) + (subStr[i] - '0'));

  for (i++;(i != ie) && (subStr[i] != ' '); i++)
    node.startColumn = ((node.startColumn * 10) + (subStr[i] - '0'));

  for (i += 3;(i != ie) && (subStr[i] != ':'); i++)
    node.endLine = ((node.endLine * 10) + (subStr[i] - '0'));

  for (i++;(i != ie) && (subStr[i] != ']'); i++)
    node.endColumn = ((node.endColumn * 10) + (subStr[i] - '0'));
  
  return node; 
}

void ScopeTree::insertNodeInList (Graph *gph, STnode node) {
  if (!gph->list.count(node.id))
    gph->list[node.id] = node;
}

std::pair<unsigned int, unsigned int> ScopeTree::buildEdge (std::string str) {
  std::pair<unsigned int, unsigned int> edge;
  unsigned int p1 = 0, p2 = 0;
  unsigned int i = 0, ie = str.size();
  
  for (;((i != ie) && (str[i] != ' ')); i++)
    p1 = ((p1 * 10) + (str[i] - '0'));

  for (i += 4;(i != ie); i++)
    p2 = ((p2 * 10) + (str[i] - '0'));

  edge = std::make_pair(p1,p2);
  return edge;
}

void ScopeTree::insertEdge (Graph *gph, unsigned int p1, unsigned int p2) {
  gph->nodes[p1].push_back(p2);
}

bool ScopeTree::readFile (std::string name, Function *F) {
  name = name + "_scope.dot";
  std::fstream Infile;
  Infile.open(name.c_str(), std::ios::in);
  if (!Infile)
    return false;
  bool graphE = false;

  while (!Infile.eof()) {
    std::string Line = std::string();
    std::getline(Infile, Line);

    if (Line == "graph {")
      graphE = true;
     
    if (Line == "}")
      graphE = false;

    if (graphE) {
      Graph gph;
      gph.file = name;
      std::getline(Infile, Line);
      std::getline(Infile, Line);
 
      // For each line, generate a node in the with target data.
      do {
        STnode node = generateSTNode(Line);
        insertNodeInList(&gph, node);
        std::getline(Infile, Line);
      } while (Line != "");

      std::getline(Infile, Line);
      std::getline(Infile, Line);
      gph.n_nodes = 0;
      // Starts to build a graph.
      for (auto I = gph.list.begin(), IE = gph.list.end(); I != IE; I++) {
        if (gph.n_nodes < I->second.id)
          gph.n_nodes = I->second.id;
      }

      gph.n_nodes++;
      std::vector<unsigned int> vct;
      // Generates a vector for vertex edge in the graph.
      for (unsigned int i = 0, ie = gph.n_nodes; i != ie; i++)
        gph.nodes.push_back(vct);

      do {
        std::pair<unsigned int, unsigned int> edge;
        edge = buildEdge(Line);
        insertEdge (&gph, edge.first, edge.second);
        std::getline(Infile, Line);
      } while (Line != "");
      
      Module *M = F->getParent();
      if (!info.count(M)) {
        std::vector<Graph> vctGph;
        info[M] = vctGph;
      }
      // Identify the parent of each node in the graph.
      identifyParents(&gph);
      info[M].push_back(gph);
    }
  }
  Infile.close();
  return true;
}

void ScopeTree::identifyParents (Graph *gph) {
  unsigned int id = INT_MAX;

  for (auto I = gph->list.begin(), IE = gph->list.end(); I != IE; I++) {
    if (I->second.id < id)
      id = I->second.id;
  }

  for (auto I = gph->list.begin(), IE = gph->list.end(); I != IE; I++) {
    if (I->second.isTopLevel) {
      id = I->second.id;
      break;
    }
  }

  std::queue<unsigned int> toIterate;
  std::vector<bool> nodeFound(gph->n_nodes, false);
  toIterate.push(id);
  nodeFound[id] = true;

  while(!toIterate.empty()) {
    id = toIterate.front();
    toIterate.pop();
    for (unsigned int i = 0, ie = gph->nodes[id].size(); i != ie; i++) {
      if (!nodeFound[gph->nodes[id][i]]) {
        toIterate.push(gph->nodes[id][i]);
        gph->parents[gph->nodes[id][i]] = id;
        nodeFound[gph->nodes[id][i]] = true;
      }
    }
  }
}

bool ScopeTree::isValidLoopStatement (STnode node, int line, int column) {
  return ((node.startLine == line) && (node.startColumn == column) &&
          ((node.name.find("WhileStmt") != string::npos) ||
           (node.name.find("DoStmt") != string::npos) ||
           (node.name.find("ForStmt") != string::npos)));
}

void ScopeTree::associateLoop (Loop *L) {
  unsigned int line = L->getStartLoc()->getLine();
  unsigned int column = L->getStartLoc()->getColumn();
  Module *M = L->getHeader()->getParent()->getParent();

  if (!info.count(M))
    return;

  for (auto I = info[M].begin(), IE = info[M].end(); I != IE; I++)
    for (auto J = I->list.begin(), JE = I->list.end(); J != JE; J++) {
      if (isValidLoopStatement(J->second, line, column)) {
        J->second.isLoop = true;
        loopNodes[L] = J->second;
        return;
      }
      MDNode* MD = L->getHeader()->getTerminator()->getMetadata("dbg");
      if (!MD)
        continue;
      if (DILocation *DL = dyn_cast<DILocation>(MD))
        column = DL->getColumn();
      if (isValidLoopStatement(J->second, line, column)) {
        J->second.isLoop = true;
        loopNodes[L] = J->second;
        return;
      }
      column = L->getStartLoc()->getColumn();
    }
}

void ScopeTree::associateFunction (Function *F) {
  Module *M = F->getParent();
  if (!info.count(M) || funcNodes.count(F))
    return;

  for (auto I = info[M].begin(), IE = info[M].end(); I != IE; I++)
    for (auto J = I->list.begin(), JE = I->list.end(); J != JE; J++) {
      if (F->getName() == J->second.name) {
        funcNodes[F] = J->second;
        return;
      }
    }
}

void ScopeTree::associateIRSource (Function *F) {
  associateFunction(F);
  std::map<Loop*, bool> loops;
 
  for (auto B = F->begin(), BE = F->end(); B != BE; B++) {
    Loop *L = li->getLoopFor(B);
    if (!L || loops.count(L))
      continue;
    loops[L] = true;
  }
  
  for (auto I = loops.begin(), IE = loops.end(); I != IE; I++)
    associateLoop(I->first);
}

void ScopeTree::printData () {
  for (auto MI = info.begin(), ME = info.end(); MI != ME; MI++) {
    for (auto GI = MI->second.begin(), GE = MI->second.end(); GI != GE;
              GI++) {
      errs() << "Number of Nodes: " << GI->n_nodes << "\n";
      errs() << "Files: " << GI->file << "\n";
      for (unsigned int i = 0, ie = GI->n_nodes; i != ie; i++) {
        for (unsigned int j = 0, je = GI->nodes[i].size();j != je; j++) {
          errs() << "EDGE : " <<  i << " - " << GI->nodes[i][j] << "\n";
        }
      }
      for (auto LI = GI->list.begin(), LE = GI->list.end(); LI != LE; LI++) {
        errs() << "--------------- NODE " << LI->first << " ----------------\n";
        errs() << "ID : " << LI->second.id << "\n";
        errs() << "Line Start : " << LI->second.startLine << "\n";
        errs() << "Column Start : " << LI->second.startColumn << "\n";
        errs() << "Line End : " << LI->second.endLine << "\n";
        errs() << "Column End : " << LI->second.endColumn << "\n";
        errs() << "Name : " << LI->second.name << "\n";
        errs() << "Is Top Level : " << LI->second.isTopLevel << "\n";
        errs() << "--------------- NODE " << LI->first << " END ------------\n";
      }
    }
  }
}

void ScopeTree::associateLoopstoRegion (std::map<Loop*, STnode> & Loops,
                                        Region *R) {
 
  for (auto BB = R->block_begin(), BE = R->block_end(); BB != BE; BB++) {
    Loop *L = li->getLoopFor(*BB);
    if (!L || Loops.count(L))
      continue;
    if (R->contains(L->getHeader()) == false)
      continue;
    Loops[L] = loopNodes[L];
  }
}

ScopeTree::Graph ScopeTree::findGraph (Region *R) {
  Function *F = R->getEntry()->getParent();
  Module *M = F->getParent();

  // Find the Top region node to start the search, and the respective graph.
  STnode node;
  Graph gph;
  for (auto I = info[M].begin(), IE = info[M].end(); I != IE; I++) {
    for (auto J = I->list.begin(), JE = I->list.end(); J != JE; J++) {
      if (J->second.name != F->getName())
        continue;
      node = J->second;
      break;
    }
    if (node.name == F->getName()) {
      gph = *I;
      break;
    }
  }
  return gph;
}

std::pair<unsigned int, unsigned int> ScopeTree::getStartRegionLoops (
     Region *R) {
  std::map<Loop*, STnode> Loops;
  associateLoopstoRegion (Loops, R);
  unsigned int minLine = DEFVAL;
  unsigned int minColumn = DEFVAL;
  for (auto I = Loops.begin(), IE = Loops.end(); I != IE; I++) {
    if (I->second.startLine < minLine) {
      minLine = I->second.startLine;
      minColumn = I->second.startColumn;
    }
    else if (minLine == I->second.startLine && 
             I->second.startColumn < minColumn) {
      minColumn = I->second.startColumn;
    }
  }
  return std::make_pair(minLine, minColumn);
}
  
std::pair<unsigned int, unsigned int> ScopeTree::getEndRegionLoops (Region *R) {
  std::map<Loop*, STnode> Loops;
  associateLoopstoRegion (Loops, R);
  unsigned int maxLine = 0;
  unsigned int maxColumn = 0;
  for (auto I = Loops.begin(), IE = Loops.end(); I != IE; I++) {
    if (I->second.endLine > maxLine) {
      maxLine = I->second.endLine;
      maxColumn = I->second.endColumn;
    }
    else if (maxLine == I->second.endLine && 
             I->second.endColumn < maxColumn) {
      maxColumn = I->second.endColumn;
    }
  }
  return std::make_pair(maxLine, maxColumn);
}
  
bool ScopeTree::isSafetlyRegionLoops (Region *R) {
  std::map<Loop*, STnode> Loops;
  associateLoopstoRegion (Loops, R);
  Function *F = R->getEntry()->getParent();
  Module *M = F->getParent();
  
  // Find the Top region node to start the search, and the respective graph.
  if (!funcNodes.count(F)) {
    return false;
  }
  STnode node = funcNodes[F];
  Graph gph = findGraph(R);

  std::queue<unsigned int> toIterate;
  toIterate.push(node.id);
  std::vector<unsigned int> nodeLevel(gph.n_nodes, DEFVAL);
  nodeLevel[node.id] = 0; 
 
  // Classify each STnode in a level, with a BFS search.
  while (!toIterate.empty()) {
    unsigned int id = toIterate.front();
    toIterate.pop();
    
    for (unsigned int i = 0, ie = gph.nodes[id].size(); i != ie; i++) {
      if ((nodeLevel[gph.nodes[id][i]] == DEFVAL) ||
           nodeLevel[gph.nodes[id][i]] > (nodeLevel[id] + 1)) {
        nodeLevel[gph.nodes[id][i]] = nodeLevel[id] + 1;
        toIterate.push(gph.nodes[id][i]);
      }
    }
  }
 
  // Find each loop's node present in this region. Compare the level, we can
  // mark as safe in two cases:
  //
  //   1 -> All loops have a unique top level.
  //   2 -> If a loop is present in a subLoop.
  unsigned int topLevel = DEFVAL;
  unsigned int topLevelNode = DEFVAL;
  for (auto I = Loops.begin(), IE = Loops.end(); I != IE; I++) {
    if ((I->second.isLoop) && (nodeLevel[I->second.id] < topLevel)) {
      topLevel = nodeLevel[I->second.id];
      topLevelNode = I->second.id;
    }
  }

  // Find a parent to be used as the reference.
  unsigned int parent = gph.parents[topLevel];

  bool valid = true;
  bool needCase2 = false;
  std::vector<Loop*> safety;
  for (auto I = Loops.begin(), IE = Loops.end(); I != IE; I++) {
    // Case 1
    if ((I->second.isLoop) && (nodeLevel[I->second.id] == topLevel) &&
        (gph.parents[topLevelNode] == gph.parents[I->second.id])) {
      safety.push_back(I->first);
      continue;
    }
    needCase2 = true;
  }
  
  // Case 2
  if (needCase2) {
    std::queue<Loop*> q;
    for (unsigned int i = 0, ie = safety.size(); i != ie; i++) {
      q.push(safety[i]);
    }
    while (!q.empty()) {
      Loop *L = q.front();
      q.pop();
      for (Loop *SubLoop : L->getSubLoops()) {
        q.push(SubLoop);
        safety.push_back(SubLoop);
      }
    }
  }

  // If exists a loop that is not present in the vector "safety",
  // this information cannot be used to insert code in the original
  // source file.
  for (auto I = Loops.begin(), IE = Loops.end(); I != IE; I++) {
    bool isSafe = false;
    for (unsigned int j = 0, je = safety.size(); j != je; j++) {
      if (I->first == safety[j]) {
        isSafe = true;
        break;
      }
    }
    if (!isSafe) {
      valid = false;
      break;
    }
  }
  return valid;
}

bool ScopeTree::runOnFunction(Function &F) {
  this->li = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  this->rp = &getAnalysis<RegionInfoPass>();
  this->aa = &getAnalysis<AliasAnalysis>();
  this->se = &getAnalysis<ScalarEvolution>();
  this->dt = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  std::string fName = getFileName(F.begin()->getTerminator());
  if ((fName != std::string()) && !isFileRead.count(fName)) {
    isFileRead[fName] = readFile(fName, &F);
  }
  
  if (isFileRead[fName])
    associateIRSource(&F);
  
  return true;
}

char ScopeTree::ID = 0;
static RegisterPass<ScopeTree> Z("scopeTree",
"Provide extra debug information.");

//===--------------------------- ScopeTree.cpp ----------------------------===//
