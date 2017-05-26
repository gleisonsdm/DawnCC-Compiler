#!/bin/bash

#You need wget, tar, unzip, cmake and a toolchain to use this script
#Put this on a root folder, than it will download all tarballs required and build
#After running, you will have something like
#
#./
#├── build.sh
#├── cfe-3.7.0.src.tar.xz
#├── DawnCC
#├── DawnCC-Compiler-master.zip
#├── llvm
#├── llvm-3.7.0.src.tar.xz
#└── llvm-build

#Number of threads to build
MAKE_THREADS=8

#Clang and LLVM versions
LLVM_VER="3.7.0"
CLANG_VER="3.7.0"

#DawnCC root path plus Clang and LLVM source folders
ROOT_FOLDER=`pwd`
DAWN_PATH="${ROOT_FOLDER}/DawnCC"
LLVM_SRC="${ROOT_FOLDER}/llvm"
CLANG_SRC="${LLVM_SRC}/tools/clang"
LLVM_OUTPUT_DIR="${ROOT_FOLDER}/llvm-build"

#Tarball names
LLVM_SRC_FILE="llvm-${LLVM_VER}.src.tar.xz"
CLANG_SRC_FILE="cfe-${CLANG_VER}.src.tar.xz"
DAWN_SRC_FILE="DawnCC-Compiler-master.zip"

#Tarball websites
LLVM_SRC_ADDR="http://llvm.org/releases/${LLVM_VER}/${LLVM_SRC_FILE}"
CLANG_SRC_ADDR="http://llvm.org/releases/${CLANG_VER}/${CLANG_SRC_FILE}"
DAWN_SRC_ADDR="https://github.com/gleisonsdm/DawnCC-Compiler/archive/master.zip"

#Download LLVM, Clang and DawnCC source tarballs if not already downloaded
if [ ! -f "${LLVM_SRC_FILE}" ]; then
    wget "${LLVM_SRC_ADDR}"
fi

if [ ! -f "${CLANG_SRC_FILE}" ]; then
    wget "${CLANG_SRC_ADDR}"
fi

if [ ! -f "${DAWN_SRC_FILE}" ]; then
    wget -O "${DAWN_SRC_FILE}" "${DAWN_SRC_ADDR}" #download master.zip as DawnCC-Compiler-master.zip
fi


#If downloaded tarballs were not extracted, then extract
if [ ! -d "${DAWN_PATH}" ]; then
    unzip "${DAWN_SRC_FILE}" 
    mv "${ROOT_FOLDER}/${DAWN_SRC_FILE%%.*}" "${DAWN_PATH}"
fi

if [ ! -d "${LLVM_SRC}" ]; then
    mkdir "${LLVM_SRC}"
    tar -Jxf "llvm-${LLVM_VER}.src.tar.xz" -C "${LLVM_SRC}" --strip 1


    #Apply DawnCC patch into LLVM source
    cd "${LLVM_SRC}"
    patch -p1 < "${DAWN_PATH}/ArrayInference/llvm-patch.diff"
    cd "${ROOT_FOLDER}"
fi

if [ ! -d "${CLANG_SRC}" ]; then
    mkdir "${CLANG_SRC}"
    tar -Jxf "cfe-${CLANG_VER}.src.tar.xz" -C "${CLANG_SRC}" --strip 1 #extract clang tarball to llvm/tools folder
fi



#Create output folder for LLVM
mkdir ${LLVM_OUTPUT_DIR}

#Setup LLVM+Clang and scope-finder plugin
mkdir -p ${LLVM_SRC}/tools/clang/tools/extra
echo "add_subdirectory(scope-finder)" > ${LLVM_SRC}/tools/clang/tools/extra/CMakeLists.txt
cp -rf ${DAWN_PATH}/ScopeFinder/scope-finder ${LLVM_SRC}/tools/clang/tools/extra/.

#Create setup with cmake
cd ${LLVM_OUTPUT_DIR}
cmake -DCMAKE_BUILD_TYPE=debug -DBUILD_SHARED_LIBS=ON ${LLVM_SRC}

#Build LLVM and Clang
make -j${MAKE_THREADS}
cd ${ROOT_FOLDER}

#Create lib folder of DawnCC
mkdir ${DAWN_PATH}/lib 

#Navigate and create setup
cd ${DAWN_PATH}/lib
cmake -DLLVM_DIR=${LLVM_OUTPUT_DIR}/share/llvm/cmake ../
make -j${MAKE_THREADS}

#Go back to root folder
cd ${ROOT_FOLDER}