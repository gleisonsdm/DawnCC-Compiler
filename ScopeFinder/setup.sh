#!/bin/bash

# Run this to setup the scope-finder plugin to be built alongside an LLVM+Clang
# build

LLVM_DIR="$1"

if [ -z "$LLVM_DIR" ]
then
	echo "Usage: $0 <LLVM_DIR>"
	exit 1
fi

mkdir -p $LLVM_DIR/tools/clang/tools/extra

echo "add_subdirectory(scope-finder)" > $LLVM_DIR/tools/clang/tools/extra/CMakeLists.txt

rm -rf $LLVM_DIR/tools/clang/tools/extra/scope-finder

cp -rf scope-finder $LLVM_DIR/tools/clang/tools/extra/.


