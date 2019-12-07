//===----------------------------ompwriterr.cpp-----------------------------===
//
//
//Author: Gleison Souza Diniz Mendonca
//  [gleison.mendonca at dcc.ufmg.br | gleison14051994 at gmail.com]
//
//===-----------------------------------------------------------------------===
//
//OMP Extractor is a small plugin developed for the Clang C compiler front-end.
//Its goal is to provide auxiliary source-code information extracting information
//of Openmp pragmas, to permits people to understand and compare different openmp
//pragmas for the same benchamrk.
//
//More specifically, it collects information about the synctatical pragma
//constructions and pragmas that exist within a C/C++ source-code file. It then
//builds a Json file, which is a representation of those pragma blocks in the source
//file, where each loop is a node block with information about parallelization using
//OpenMP syntax.
//
//For each input file, its reference nodes are outputted as a JSON format file, that
//represents the loops inside the source code.
//
//Since it is a small self-contained plugin (not meant to be included by other
//applications), all the code is kept within its own source file, for simplici-
//ty's sake.
//
//By default, the plugin is built alongside an LLVM+Clang build, and its shared
//library file (ompextractor.so) can be found its build.
//
//The plugin can be set to run during any Clang compilation command, using the
//following syntax:
//
//  clang -Xclang -load -Xclang $SCOPE -Xclang -add-plugin -Xclang -extract-omp
//
//  Where $SCOPE -> points to the ompextractor.so shared library file location 
//===-----------------------------------------------------------------------===

#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include <stack>
#include <map>
#include <vector>
#include <fstream>

using namespace std;
using namespace clang;
using namespace llvm;

  //---------------------------------------------------------------------------
  //                       DATA SCTRUCTURES
  //---------------------------------------------------------------------------
  /*we can use this little baby to alter the original source code, if we ever feel
  like it*/
  Rewriter rewriter;

/*visitor class, inherits clang's ASTVisitor to traverse specific node types in
 the program's AST and retrieve useful information*/
class PragmaVisitor : public RecursiveASTVisitor<PragmaVisitor> {
private:
    ASTContext *astContext; //provides AST context info
    MangleContext *mangleContext;

public:
    
    explicit PragmaVisitor(CompilerInstance *CI) 
      : astContext(&(CI->getASTContext())) { // initialize private members
        rewriter.setSourceMgr(astContext->getSourceManager(),
        astContext->getLangOpts());
    }

    /*Recover C code */
    std::string getSourceSnippet(SourceRange sourceRange, bool allTokens, bool jsonForm) {
      if (!sourceRange.isValid())
	return std::string();

      SourceLocation bLoc(sourceRange.getBegin());
      SourceLocation eLoc(sourceRange.getEnd());
	   
      const SourceManager& mng = astContext->getSourceManager();
      std::pair<FileID, unsigned> bLocInfo = mng.getDecomposedLoc(bLoc);
      std::pair<FileID, unsigned> eLocInfo = mng.getDecomposedLoc(eLoc);
      FileID FID = bLocInfo.first;
      unsigned bFileOffset = bLocInfo.second;
      unsigned eFileOffset = eLocInfo.second;
      int length = eFileOffset - bFileOffset;

      if (length <= 0)
	return std::string();

      bool Invalid = false;
      const char *BufStart = mng.getBufferData(FID, &Invalid).data();
      if (Invalid)
        return std::string();

      if (allTokens == true) {
	while (true) {
	  if (BufStart[(bFileOffset + length)] == ';')
            break;
	  if (BufStart[(bFileOffset + length)] == '}')
	    break;
	  if (length == eFileOffset)
            break;
          length++;
	}
      }

      if (length != eFileOffset)
        length++;

      std::string snippet = StringRef(BufStart + bFileOffset, length).trim().str();
      snippet = replace_all(snippet, "\\", "\\\\");
      snippet = replace_all(snippet, "\"", "\\\"");

      if (jsonForm == true)
	snippet = "\"" + replace_all(snippet, "\n", "\",\n\"") + "\"";

      return snippet;
    }

    /*Replace all  occurrences in the target string*/
    std::string replace_all(std::string str, std::string from, std::string to) {
      int pos = 0;
      while((pos = str.find(from, pos)) != std::string::npos) {
	str.replace(pos, from.length(), to);
	pos = pos + to.length();
      }
      return str;
    }

    /*visit each node walking in the sub-ast and provide a list stored as "nodes_list"*/
    void visitNodes(Stmt *st, vector<Stmt*> & nodes_list) {
      if (!st)
	return;
      nodes_list.push_back(st);
      if (CapturedStmt *CPTSt = dyn_cast<CapturedStmt>(st)) {
        visitNodes(CPTSt->getCapturedStmt(), nodes_list);
	return;
      }
      for (auto I = st->child_begin(), IE = st->child_end(); I != IE; I++) {
       visitNodes((*I)->IgnoreContainers(true), nodes_list);
      }
    }

    /*visits all nodes of type decl*/
    virtual bool VisitDecl(Decl *D) {
	if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      	  if (FD->doesThisDeclarationHaveABody()) {
	    const SourceManager& mng = astContext->getSourceManager();
            if (astContext->getSourceManager().isInSystemHeader(D->getLocation())) {
              return true;
            }

	  }
	}
	// Add an else here, to find other declarations.
      return true;
    }

};

class PragmaASTConsumer : public ASTConsumer {
private:
    PragmaVisitor *visitor; // doesn't have to be private

public:
    /*override the constructor in order to pass CI*/
    explicit PragmaASTConsumer(CompilerInstance *CI)
        : visitor(new PragmaVisitor(CI)) // initialize the visitor
    { }

    /*we override HandleTranslationUnit so it calls our visitor
    after parsing each entire input file*/
    virtual void HandleTranslationUnit(ASTContext &Context) {
        /*traverse the AST*/
        visitor->TraverseDecl(Context.getTranslationUnitDecl());

    }
};

class PragmaPluginAction : public PluginASTAction {
protected:
    /*This gets called by Clang when it invokes our Plugin.
    Has to be unique pointer (this bit was a bitch to figure out*/
    unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, 
                                              StringRef file) {
        return make_unique<PragmaASTConsumer>(&CI);
    }

    /*leaving this here as a placeholder for now, we can implement a function
    here to evaluate and handle input arguments, if ever necessary*/
    bool ParseArgs(const CompilerInstance &CI, const vector<string> &args) {
      for (unsigned i = 0, e = args.size(); i != e; ++i) {
        // Add code to process parameters
      }
      return true;
    }
};

/*register the plugin and its invocation command in the compilation pipeline*/
static FrontendPluginRegistry::Add<PragmaPluginAction> X
                                               ("-private-detector", "Private Detector");
