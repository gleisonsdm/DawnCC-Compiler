#!/bin/bash

THIS=$(pwd)
cd "${THIS}/../llvm-build/"
LLVM_OUTPUT_DIR=$(pwd)
cd "${THIS}"

cd "${THIS}/PrivateDetector/"
PRIVATE_PATH=$(pwd)
cd "${THIS}"

if [ ! -f "${THIS}/libPrivate" ]; then
   rm -r "${THIS}/libPrivate"
   mkdir "${THIS}/libPrivate"
fi

cd "${THIS}/libPrivate"
CXX=g++-8 cmake -DLLVM_DIR=${LLVM_OUTPUT_DIR}/share/llvm/cmake ${PRIVATE_PATH}
make -j4
cd "${THIS}"
