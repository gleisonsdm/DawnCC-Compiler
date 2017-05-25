#!/bin/bash

#You can use this script to run DawnCC, passing the DawnCC root dir, arguments to change output and files to be processed
#./run.sh -d (DawnCC root dir - containing DawnCC and llvm-build) -src (folder with *.c and *.cpp files to be processed)


#Set default parameters of DawnCC
CURRENT_DIR=`pwd`
DEFAULT_ROOT_DIR=`pwd`
GPUONLY_BOOL="false"
PARALELLIZE_LOOPS_BOOL="true"
PRAGMA_STANDARD_INT=1
POINTER_DESAMBIGUATION_BOOL="true"
MEMORY_COALESCING_BOOL="true"
MINIMIZE_ALIASING_BOOL="true"
CODE_CHANGE_BOOL="true"
FILES_FOLDER="src"


#Process arguments of script
while [[ $# -gt 1 ]]
do
    key="$1"

    case $key in
        -d|--DawnCCRoot)
            DEFAULT_ROOT_DIR="$2" #if you pass the DawnCC root dir (folder containing llvm-build and DawnCC, it's not going to assume that you're currently on it)
            shift # past argument
        ;;
        -G|--GpuOnlyAnalysis)
            GPUONLY_BOOL="$2" # true - analyze only functions with GPU prefix; false - analyze all functions
            shift # past argument
        ;;
        -pl|--ParallelizeLoops)
            PARALELLIZE_LOOPS_BOOL="$2" #true - try to annotate loops; false - don't annotate loops
            shift # past argument
        ;;
        -ps|--PragmaStandard)
            PRAGMA_STANDARD_INT="$2" #0-OpenACC; 1-OpenMP GPU; 2-OpenMP CPU
            shift # past argument
        ;;
        -pd|--PointerDesambiguation)
            POINTER_DESAMBIGUATION_BOOL="$2" #true - annotate tests; false - don't annotate tests
            shift # past argument
        ;;
        -mc|--MemoryCoalescing)
            MEMORY_COALESCING_BOOL="$2" #true - try to use memory coalescing; false - don't try to use coalescing
            shift # past argument
        ;;
        -ma|--MinimizeAliasing)
            MINIMIZE_ALIASING_BOOL="$2" #true - use licm in pointer range analysis; false - don't make pointer range analysis
            shift # past argument
        ;;
        -cc|--AllowCodeChange)
            CODE_CHANGE_BOOL="$2" #true - allow modification to program regions; false - only modify what allowed in LLVM IR
            shift # past argument
        ;;
        -src|--SourceFolder)
            FILES_FOLDER="$2" # path to be scanned and have files processed
            shift
        ;;
        *)
            # unknown option
        ;;
    esac
    shift # past argument or value
done


#Export path to llvm and its tools
LLVM_PATH="${DEFAULT_ROOT_DIR}/llvm-build/"
export CLANG="${LLVM_PATH}/bin/clang"
export CLANGFORM="${LLVM_PATH}/bin/clang-format"
export OPT="${LLVM_PATH}/bin/opt"
export SCOPEFIND="${LLVM_PATH}/lib/scope-finder.so"

#Export path to DawnCC libraries
export BUILD="${DEFAULT_ROOT_DIR}/DawnCC/lib"
export PRA="${BUILD}/PtrRangeAnalysis/libLLVMPtrRangeAnalysis.so"
export AI="${BUILD}/AliasInstrumentation/libLLVMAliasInstrumentation.so"
export DPLA="${BUILD}/DepBasedParallelLoopAnalysis/libParallelLoopAnalysis.so"
export CP="${BUILD}/CanParallelize/libCanParallelize.so"
export WAI="${BUILD}/ArrayInference/libLLVMArrayInference.so"
export ST="${BUILD}/ScopeTree/libLLVMScopeTree.so"

#Export tools flags
export FLAGS="-mem2reg -tbaa -scoped-noalias -basicaa -functionattrs -gvn -loop-rotate
-instcombine -licm"

export FLAGSAI="-mem2reg -instnamer -loop-rotate"


#Temporary files names
TEMP_FILE1="result.bc"
TEMP_FILE2="result2.bc"
TEMP_FILE3="result3.bc"

if [ -f "${TEMP_FILE1}" ]; then
    rm ${TEMP_FILE1}
fi

if [ -f "${TEMP_FILE2}" ]; then
    rm {TEMP_FILE2}
fi

cd ${FILES_FOLDER}

for f in $(find . -name '*.c' -or -name '*.cpp'); do 
    $CLANGFORM -style="{BasedOnStyle: llvm, IndentWidth: 2}" -i ${f}

    $CLANG -Xclang -load -Xclang $SCOPEFIND -Xclang -add-plugin -Xclang -find-scope -g -O0 -c -fsyntax-only ${f}

    $CLANG -g -S -emit-llvm ${f} -o ${TEMP_FILE1} 

    $OPT -load $PRA -load $AI -load $DPLA -load $CP $FLAGS -ptr-ra -basicaa \
      -scoped-noalias -alias-instrumentation -region-alias-checks -can-parallelize -S ${TEMP_FILE1}

    $OPT -load $ST -load $WAI -annotateParallel -S ${TEMP_FILE1} -o ${TEMP_FILE2}

    $OPT -S $FLAGSAI -load $ST -load $WAI -writeInFile -stats -Emit-GPU=${GPUONLY_BOOL} \
      -Emit-Parallel=${PARALELLIZE_LOOPS_BOOL} -Emit-OMP=${PRAGMA_STANDARD_INT} -Restrictifier=${POINTER_DESAMBIGUATION_BOOL} \
      -Memory-Coalescing=${MEMORY_COALESCING_BOOL} -Ptr-licm=${MINIMIZE_ALIASING_BOOL} -Ptr-region=${CODE_CHANGE_BOOL} \
      -Run-Mode=false ${TEMP_FILE2} -o ${TEMP_FILE3}

    #$CLANGFORM -style="{BasedOnStyle: llvm, IndentWidth: 2}" -i "${f}"
done

cd ${CURRENT_DIR}

