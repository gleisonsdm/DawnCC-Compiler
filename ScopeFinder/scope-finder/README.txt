This is a Clang plugin to analyze C/C++ source code and extract source-code
scope information. The goal is to analyze which syntax in code defines
independent scopes, and build a tree that represents scope hierarchy and each
scope's syntax definition in the original source code.

At the moment, the plugin can be built by setting up an LLVM+Clang build using
CMake that points to a source directory that contains this folder and its
parent's CMakeLists.txt.

Once the plugin is built, you can run it using:
--
Linux:
$ clang -Xclang -load llvm_dir/lib/scope-finder.so -Xclang -add-plugin -Xclang\
-find-scope -g -O0 -c -fsyntax-only [input1.c input2.c ...]
