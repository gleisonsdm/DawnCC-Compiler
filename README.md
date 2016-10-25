# DawnCC - A Source to Source Compiler 

## Introduction

### Motivation

In recent times, there has been an increasing interest in general-purpose computing on graphics processing units (GPGPU). This practice consists of developing general purpose programs, i.e., not necessarily related to graphics processing, to run on hardware that is specialized for graphics computing. Executing programs on such chips can be advantageous due to the parallel nature of their architecture: while a typical CPU is composed of a small number of cores capable of a wide variety of computations, GPUs usually contain hundreds of simpler processors, which perform operations in separate chunks of memory concurrently. Thus, a graphics chip can run programs that are sufficiently parallel much faster than a CPU. In some cases, this speedup can reach several orders of magnitude. Some experiments also show that not only can GPU execution be faster, but in many cases they are also more energy-efficient. 

This model, however, has its shortcomings. Historically, parallel programming has been a difficult paradigm to adopt, usually requiring that developers be familiarized with particular instruction sets of different graphics chips. Recently, a few standards such as OpenCL and CUDA have been designed to provide some level of abstraction to these platforms, which in turn has led to the development of several compiler directive-based models, e.g. OpenACC and offloading in OpenMP. While these have somewhat bridged the gap between programmers and parallel programming interfaces, they still rely on manual insertion of compiler directives in production code, an error-prone process that also commands in-depth knowledge of the target program. 

Amongst the hurdles involved in annotating code, two tasks are particularly challenging: identifying parallel loops and estimating memory bounds. Regarding the former, opportunities for parallelism are usually buried in complex code, and rely on non-trivially verifiable information, such as the absence of pointer aliasing. As to the latter, languages such as C and C++ do not provide any information on the size of memory being accessed during the execution of the program. However, when offloading code for parallel execution, it is necessary to inform which chunks of memory must be copied to other devices. Therefore, the onus of keeping track of these bounds falls on the programmer.

### Implementation

We have developed DawnCC as a tool to automate the performance of these tasks. Through the implementation of a static analysis that derives memory access bounds from source code, it infers the size of memory regions in C and C++ programs. With these bounds, it is capable of inserting data copy directives in the original source code. These directives provide a compatible compiler with information on which data must be moved between devices. Given the source code of a program as input, our tool can then provide the user with a modified version containing directives with proper memory bounds specified, all without any further intervention from the user, effectively freeing developers from the burdensome task of manual code modification. 

We implemented DawnCC as a collection of compiler modules, or passes, for the LLVM compiler infrastructure, with this webpage functioning as a front-end for users to provide program source codes as input. The program is then compiled by LLVM and transformed by our passes to produce the modified source code as output to the webpage user. There are also a few options to customize the output, such as printing runtime statistics.

## Functionality

### Compiler Directive Standards

Compiler directive-oriented programming standards are some of the newest developments in features for parallel programming. These standards aim to simplify the creation of parallel programs by providing an interface for programmers to indicate specific regions in source code to be run as parallel. Parallel execution has application in several different hardware settings, such as multiple processors in a multicore architecture, or offloading to a separate device in a heterogeneous system. Compilers that support these standards can check for the presence of directives (also known as pragmas) in the source code, and generate parallel code for the specific regions annotated, so they can be run on a specified target device. DawnCC currently supports two standards, OpenACC and OpenMP, but it can easily be extended to support others. You can read more on the subject in the links below:

[OpenACC FAQ](http://www.openacc.org/faq-questions-inline)

[OpenACC MP](http://openmp.org/openmp-faq.html)

In order to use these standards to offload execution to accelerators, it is necessary to compile the modified source code with a compiler that supports the given directive standard (OpenMP 4.0 or OpenACC). In our internal testing environment, we use Portland Group's C Compiler for OpenACC support. You can find out more about it in the following link:

[Portland Group](http://www.pgroup.com/index.htm)

## Installation

The project is structured as a set of dynamically loaded libraries/passes for LLVM that can be built separate from the main compiler. However, an existing LLVM build (compiled using cmake) is necessary to build our code. The base LLVM version used in this project was LLVM 3.7 release:

[LLVM](http://llvm.org/releases/3.7.0/llvm-3.7.0.src.tar.xz)

[Clang](http://llvm.org/releases/3.7.0/cfe-3.7.0.src.tar.xz)

This project also requires some changes to be applied to LLVM itself. To do so, apply the patch "llvm-patch.diff" to your LLVM source directory. This path can be find in 'ArrayInference/llvm-patch.diff'.

	MAKEFLAG="-j8"
  
 	REPO=< path-to-repository >

	# Build a debug version of LLVM+Clang under ${REPO}/build-debug
	mkdir ${REPO}/build-debug
	cd ${REPO}/build-debug
	cmake -DCMAKE_BUILD_TYPE=debug -DBUILD_SHARED_LIBS=ON ${REPO}/llvm-src/
	make ${MAKEFLAG}
	cd -

After you get a fresh LLVM build under ${LLVM_BUILD_DIR}, the following commands can be used to build DawnCC:

 	REPO=< path-to-repository >

 	# Build the code under ${REPO}/build-release, assumming an existing LLVM
 	# build under ${LLVM_BUILD_DIR}
 	mkdir ${REPO}/build-debug
 	cd ${REPO}/build-debug
 	cmake -DLLVM_DIR=${LLVM_BUILD_DIR}/share/llvm/cmake ../src/
 	make
	cd -

## How to run a code

To run DawnCC, copy and paste the text below into a script file. You will have to change text between pointy brackets, e.g., *< like this >* to adapt the script to your environment.

 	LLVM_PATH=< llvm-3.7-src/build-debug/bin >

 	export CLANG="$LLVM_PATH/clang"
 	export CLANGFORM="$LLVM_PATH/clang-format"
 	export OPT="$LLVM_PATH/opt"
 	export LINKER="$LLVM_PATH/llvm-link"
 	export DIS="$LLVM_PATH/llvm-dis"

 	export BUILD=< DawnCC/build-debug >

 	export PRA="$BUILD/PtrRangeAnalysis/libLLVMPtrRangeAnalysis.so"
 	export AI="$BUILD/AliasInstrumentation/libLLVMAliasInstrumentation.so"
 	export DPLA="$BUILD/DepBasedParallelLoopAnalysis/libParallelLoopAnalysis.so"
 	export CP="$BUILD/CanParallelize/libCanParallelize.so"
 	export PLM="$BUILD/ParallelLoopMetadata/libParallelLoopMetadata.so"
 	export WAI="$BUILD/ArrayInference/libLLVMArrayInference.so"
 	export ST="$BUILD/ScopeTree/libLLVMScopeTree.so"

 	export XCL="-Xclang -load -Xclang"
 	
	export FLAGS="-mem2reg -tbaa -scoped-noalias -basicaa -functionattrs -gvn -loop-rotate
 	-instcombine -licm"
 	export FLAGSAI="-mem2reg -instnamer -loop-rotate"

 	export RES="result.bc"

 	rm result.bc result2.bc

 	$CLANGFORM -style="{BasedOnStyle: llvm, IndentWidth: 2}" < Source Code > &> tmp.txt
 	mv tmp.txt < Source Code >

 	./scopetest.sh < Source Code >

 	$CLANG $OMP -g -S -emit-llvm < Source Code > -o result.bc 

 	#$OPT -load $ST -scopeTree result.bc 

 	$OPT -load $PRA -load $AI -load $DPLA -load $CP $FLAGS -ptr-ra -basicaa \
 	  -scoped-noalias -alias-instrumentation -region-alias-checks \ 
 	  -can-parallelize -S result.bc

 	$OPT -load $ST -load $WAI -annotateParallel -S result.bc -o result2.bc

 	$OPT -S $FLAGSAI -load $ST -load $WAI -writeInFile -stats -Emit-GPU=< op1 > \
 	  -Emit-Parallel=< op2 > -Emit-OMP=< op3 > -Restrictifier=< op4 > \
 	  -Memory-Coalescing=< op5 > -Ptr-licm=< op6 > -Ptr-region=< op7 > result2.bc -o result3.bc

 	$CLANGFORM -style="{BasedOnStyle: llvm, IndentWidth: 2}" < Source Code > &> tmp.txt

 	mv tmp.txt < Source Code >

Below, a summary of each part where it is necessary to change text:

- llvm-3.7-src/build-debug/bin : A reference to the location of the llvm-3.7 binaries. 

- DawnCC/build-debug : A reference to the location of the DawnCC binaries. 

- Source Code : The input file that will be used to run the analyses. 

- op1 => boolean that decides if the tool will analyze just the
  functions starting with "GPU__" or all functions in the source file. 
  
    true : Analyze all functions. 
    
    false : Just Analyze the functions starting with "GPU__". 
    
- op2 => boolean that decides if the tool will analyze and annotate
  parallel loops. 
  
    true : Annotate loops as parallel, if possible. 
    
    false : Do not annotate loops as parallel. 
    
- op3 => Generates all pragmas in OpenMP. 

    2 : Annotate pragmas with OpenMP directives (CPU standard format). 
    
    1 : Annotate pragmas with OpenMP directives (GPU standard format). 
    
    0 : Annotate with default pragmas (OpenACC). 
    
- op4 => Write tests to disambiguate pointers. 

    true : Annotate tests. 
    
    false : Do not annotate tests. 
    
- op5 => Try to do memory coalescing to avoid data transference. 

    true : Try to use regions to do the coalescing. 
    
    false : Do not use memory coalescing. 
    
- op6 => Try to use loop invariant code motion, to avoid alias impact. 

    true : Uses licm in Pointer Range Analysis, case necessary. 
    
    false : Do not use Pointer Range Analysis with licm. 
    
- op7 => Try to rebuild regions, and analyze each new region defined. 
  
    true : Try to rewrite regions. 
    
    false : Use just the regions available in the IR. 





