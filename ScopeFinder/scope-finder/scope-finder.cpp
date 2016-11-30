//===--------------------------scope-finder.cpp-----------------------------===
//
//
//Author: Breno Campos [brenosfg@dcc.ufmg.br | brenocfg@gmail.com]
//
//===-----------------------------------------------------------------------===
//
//Scope finder is a small plugin developed for the Clang C compiler front-end.
//Its goal is to provide auxiliary source-code information to our LLVM optimi-
//zation passes, to improve their capabilities of writing LLVM IR back to
//source code.
//
//More specifically, it collects information about the synctatical scope blocks
//that exist within a C/C++ source-code file. It then builds a Scope Tree,
//which is a hierarchical representation of those scope blocks as a tree, where
//each child node's scope block must be entirely contained in its parent's
//scope block in the code's syntax.
//
//For each input file, its Scope Tree is outputted as a DOT format file, that
//represents the tree. If the user so chooses, the DOT files can be printed to
//PNG/PDF using tools such as Graphviz.
//
//Since it is a small self-contained plugin (not meant to be included by other
//applications), all the code is kept within its own source file, for simplici-
//ty's sake.
//
//By default, the plugin is built alongside an LLVM+Clang build, and its shared
//library file (scope-finder.so) can be found in the /lib/ folder within the
//LLVM build folder.
//
//The plugin can be set to run during any Clang compilation command, using the
//following syntax:
//
//  clang -Xclang -load -Xclang $SCOPE -Xclang -add-plugin -Xclang -find-scope
//
//  Where $SCOPE -> points to the scope-finder.so shared library file location 
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
#include <fstream>

using namespace std;
using namespace clang;
using namespace llvm;

/*we can use this little baby to alter the original source code, if we ever feel
like it*/
Rewriter rewriter;

/*POD struct that represents a meaningful node in the AST, with its unique name
identifier and source location numbers*/
struct Node {
  string name;
  unsigned int id;
  unsigned int sline, scol;
  unsigned int eline, ecol;
};

/*POD struct that represents an input file in a Translation Unit (a single
source/header file). Each input file will have its own stack of traversable
nodes, and output file + associated scope tree information*/
struct InputFile {
	string filename;
	string edges;
	string labels;
	stack <struct Node> NodeStack;
};

/*we need a stack of active input files, to know which constructs belong to
which file*/
stack <struct InputFile> FileStack;

/*node counter, to uniquely identify nodes*/
long long int opCount = 0;

/*visitor class, inherits clang's ASTVisitor to traverse specific node types in
 the program's AST and retrieve useful information*/
class ScopeVisitor : public RecursiveASTVisitor<ScopeVisitor> {
private:
    ASTContext *astContext; //provides AST context info
    MangleContext *mangleContext;

public:
    explicit ScopeVisitor(CompilerInstance *CI) 
      : astContext(&(CI->getASTContext())) { // initialize private members
        rewriter.setSourceMgr(astContext->getSourceManager(),
        astContext->getLangOpts());
    }

    /*returns whether Node N is descendant of Top (Node on top of the stack) in
    the AST*/
    bool isNodeDescendant (struct Node N, struct Node Top) {
      if (N.sline < Top.sline || N.eline > Top.eline) {
        return false;
      }

      if (N.sline >= Top.sline && N.eline < Top.eline) {
        return true;
      }

      if (N.sline > Top.sline && N.eline <= Top.eline) {
        return true;
      }

      if (N.scol >= Top.scol && N.ecol <= Top.ecol) {
        return true;
      }

      return false;
    }
    
    /*manages stack of nodes given a new node to be included, and computes
    the node's edge from its parent*/
    void ProcessNode (struct Node N) {
      struct InputFile& currFile = FileStack.top();

      if (!currFile.NodeStack.empty()) {
        while (!isNodeDescendant(N, currFile.NodeStack.top())) {
          currFile.NodeStack.pop();
        }
      }

      currFile.edges += 
	to_string(currFile.NodeStack.top().id)+" -- "+to_string(N.id)+"\n";

      /*push node to top of stack, making it our current "parent candidate"*/
      currFile.NodeStack.push(N);
    }

    /*creates Node struct for a Stmt type or subtype*/
    struct Node CreateStmtNode(Stmt *st) {
        struct Node N;
        struct InputFile& currFile = FileStack.top();

        FullSourceLoc StartLocation = astContext->getFullLoc(st->getLocStart());
        FullSourceLoc EndLocation = astContext->getFullLoc(st->getLocEnd());

        if (StartLocation.isValid() == false) {
          N.sline = -1;
          return N;
        }
        
        N.id = opCount++;
        N.sline = StartLocation.getSpellingLineNumber();
        N.scol = StartLocation.getSpellingColumnNumber();
        N.eline = EndLocation.getSpellingLineNumber();
        N.ecol = EndLocation.getSpellingColumnNumber();
        N.name = st->getStmtClassName() + to_string(N.id);

        currFile.labels += to_string(N.id) + " [label=\"" + N.name + "\\n";
        currFile.labels += "[" + to_string(N.sline) + ":" + to_string(N.scol);
        currFile.labels += " - "+to_string(N.eline)+":"+to_string(N.ecol);
        currFile.labels += "]\"];\n";

        return N;
    }

    /*creates node struct for a Decl type or subtype*/
    struct Node CreateDeclNode(NamedDecl *D) {
        struct Node N;
        struct InputFile& currFile = FileStack.top();

        FullSourceLoc StartLocation = astContext->getFullLoc(D->getLocStart());
        FullSourceLoc EndLocation = astContext->getFullLoc(D->getLocEnd());

        if (StartLocation.isValid() == false) {
          N.sline = -1;
          return N;
        }

        /*we need a mangle context to tell whether mangling is necessary*/
        string FuncName;
        mangleContext = astContext->createMangleContext();

        /*retrieve mangled name when appropriate, otherwise use plain*/
        if (mangleContext->shouldMangleDeclName(D)) {
          raw_string_ostream Stream(FuncName);
          mangleContext->mangleName(D, Stream);
          FuncName = Stream.str();
        }
        else {
          FuncName = D->getNameAsString();
        }
        
        N.id = opCount++;
        N.sline = StartLocation.getSpellingLineNumber();
        N.scol = StartLocation.getSpellingColumnNumber();
        N.eline = EndLocation.getSpellingLineNumber();
        N.ecol = EndLocation.getSpellingColumnNumber();
        N.name = FuncName;

        currFile.labels += to_string(N.id) + " [shape=\"box\" ";
        currFile.labels += "label=\"" + N.name + "\\n" + "[";
        currFile.labels += to_string(N.sline)+":"+to_string(N.scol)+" - ";
        currFile.labels += to_string(N.eline)+":"+to_string(N.ecol)+"]\"];\n";

        return N;
    }

    /*Initializes a new input file and pushes it to the top of the file stack*/
    void NewInputFile(string filename) {
      struct InputFile newfile;
      struct Node root;

      newfile.filename = filename;
      FileStack.push(newfile);

      root.id = opCount++;
      root.name = filename;
      root.sline = 0;
      root.scol = 0;
      root.eline = ~0;
      root.ecol = ~0;

      /*create parent node for the new file's scope tree*/
      FileStack.top().NodeStack.push(root);
      FileStack.top().labels += to_string(root.id) + " [label=\"File: ";
      FileStack.top().labels += filename + "\"" + " shape=\"triangle\"];\n";
    }      

    /*returns whether the statement's type is a potential scope creator*/
    bool isScopeStmt(Stmt *st) {
      if (isa<CompoundStmt>(st) || isa<WhileStmt>(st) || isa<CXXCatchStmt>(st)
         || isa<CXXForRangeStmt>(st) || isa<CXXTryStmt>(st) || isa<DoStmt>(st)
         || isa<ForStmt>(st) || isa<IfStmt>(st) || isa<SEHExceptStmt>(st)
         || isa<SEHFinallyStmt>(st) || isa<SEHFinallyStmt>(st)
         || isa<SwitchCase>(st) || isa<SwitchStmt>(st) || isa<WhileStmt>(st)) {
        return true;
      }

      return false;
    }

    /*visits all Function Declaration nodes*/
    virtual bool VisitFunctionDecl(FunctionDecl *D) {
        struct Node newDecl;
	const SourceManager& mng = astContext->getSourceManager();

        /*ignore decls from system headers (stdio, iostream, etc.)*/
        if (mng.isInSystemHeader(D->getLocation())) {
          return true;
        }

        string filename = mng.getFilename(D->getLocStart());

        if (FileStack.empty() || FileStack.top().filename != filename) {
          NewInputFile(filename);
        }

        newDecl = CreateDeclNode(D);

        if (newDecl.sline > 0) {
          ProcessNode(newDecl);
        }

        return true;
    }

    /*visits all nodes of type stmt*/
    virtual bool VisitStmt(Stmt *st) {
        struct Node newStmt;

        /*skip non-scope generating statements (returning true resumes AST
        traversal)*/
        if (!isScopeStmt(st)) {
          return true;
        }

        /*ignore statements from system headers (stdio, iostream, etc)*/
        if (astContext->getSourceManager().isInSystemHeader(st->getLocStart())) {
          return true;
        }

        newStmt = CreateStmtNode(st);

        if (newStmt.sline > 0) {
          ProcessNode(newStmt);
        }

        return true;
    }
};



class ScopeASTConsumer : public ASTConsumer {
private:
    ScopeVisitor *visitor; // doesn't have to be private

public:
    /*override the constructor in order to pass CI*/
    explicit ScopeASTConsumer(CompilerInstance *CI)
        : visitor(new ScopeVisitor(CI)) // initialize the visitor
    { }

    /*empties node stack (in between different translation units)*/
    void EmptyStack() {
      while (!FileStack.empty()) {
        FileStack.pop();
      }
    }

    /*writes scope dot file as output*/
    bool writeDotToFile() {
      struct InputFile& currFile = FileStack.top(); 
      ofstream outfile;

      /*make sure we have a valid filename (input file could be empty, etc.)*/
      if (currFile.filename.empty()) {
        return false;
      }

      outfile.open(currFile.filename + "_scope.dot");

      /*couldn't open output file (might be a permissions issue, etc.)*/
      if (!outfile.is_open()) {
        return false;
      }

      /*output graph in DOT notation*/
      outfile << "graph {\n\n";
      outfile << currFile.labels << "\n\n" << currFile.edges << "\n}";

      return true;
    }

    /*we override HandleTranslationUnit so it calls our visitor
    after parsing each entire input file*/
    virtual void HandleTranslationUnit(ASTContext &Context) {
        /*traverse the AST*/
        visitor->TraverseDecl(Context.getTranslationUnitDecl());

        /*write output DOT file*/
        while (!FileStack.empty()) {
          if (writeDotToFile()) {
            errs() << "Scope info for file " << FileStack.top().filename;
            errs() << " written successfully!\n";
          }

          else {
            errs() << "Failed to write dot file for input file: ";
            errs() << FileStack.top().filename << "\n";
          }

          FileStack.pop();
        } 
    }
};

class ScopePluginAction : public PluginASTAction {
protected:
    /*This gets called by Clang when it invokes our Plugin.
    Has to be unique pointer (this bit was a bitch to figure out*/
    unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, 
                                              StringRef file) {
        return make_unique<ScopeASTConsumer>(&CI);
    }

    /*leaving this here as a placeholder for now, we can implement a function
    here to evaluate and handle input arguments, if ever necessary*/
    bool ParseArgs(const CompilerInstance &CI, const vector<string> &args) {
        return true;
    }
};

/*register the plugin and its invocation command in the compilation pipeline*/
static FrontendPluginRegistry::Add<ScopePluginAction> X
                                               ("-find-scope", "Scope Finder");
