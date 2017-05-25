# DawnCC - A Source to Source Compiler for Parallelizing C/C++ Programs with Code Annotation

[Project Webpage](http://cuda.dcc.ufmg.br/dawn)

[Code Repository](https://github.com/gleisonsdm/DawnCC-Compiler/)

## Introduction

### Motivation

In recent times, there has been an increasing interest in general-purpose computing on graphics processing units (GPGPU). This practice consists of developing general purpose programs, i.e., not necessarily related to graphics processing, to run on hardware that is specialized for graphics computing. Executing programs on such chips can be advantageous due to the parallel nature of their architecture: while a typical CPU is composed of a small number of cores capable of a wide variety of computations, GPUs usually contain hundreds of simpler processors, which perform operations in separate chunks of memory concurrently. Thus, a graphics chip can run programs that are sufficiently parallel much faster than a CPU. In some cases, this speedup can reach several orders of magnitude. Some experiments also show that not only can GPU execution be faster, but in many cases they are also more energy-efficient. 

This model, however, has its shortcomings. Historically, parallel programming has been a difficult paradigm to adopt, usually requiring that developers be familiarized with particular instruction sets of different graphics chips. Recently, a few standards such as OpenCL and CUDA have been designed to provide some level of abstraction to these platforms, which in turn has led to the development of several compiler directive-based models, e.g. OpenACC and offloading in OpenMP. While these have somewhat bridged the gap between programmers and parallel programming interfaces, they still rely on manual insertion of compiler directives in production code, an error-prone process that also commands in-depth knowledge of the target program. 

Amongst the hurdles involved in annotating code, two tasks are particularly challenging: identifying parallel loops and estimating memory bounds. Regarding the former, opportunities for parallelism are usually buried in complex code, and rely on non-trivially verifiable information, such as the absence of pointer aliasing. As to the latter, languages such as C and C++ do not provide any information on the size of memory being accessed during the execution of the program. However, when offloading code for parallel execution, it is necessary to inform which chunks of memory must be copied to other devices. Therefore, the onus of keeping track of these bounds falls on the programmer.

### Implementation

We have developed DawnCC as a tool to automate the performance of these tasks. Through the implementation of a static analysis that derives memory access bounds from source code, it infers the size of memory regions in C and C++ programs. With these bounds, it is capable of inserting data copy directives in the original source code. These directives provide a compatible compiler with information on which data must be moved between devices. Given the source code of a program as input, our tool can then provide the user with a modified version containing directives with proper memory bounds specified, all without any further intervention from the user, effectively freeing developers from the burdensome task of manual code modification. 

We implemented DawnCC as a collection of compiler modules, or passes, for the LLVM compiler infrastructure, whose code is available in this repository.

## Functionality

### Compiler Directive Standards

Compiler directive-oriented programming standards are some of the newest developments in features for parallel programming. These standards aim to simplify the creation of parallel programs by providing an interface for programmers to indicate specific regions in source code to be run as parallel. Parallel execution has application in several different hardware settings, such as multiple processors in a multicore architecture, or offloading to a separate device in a heterogeneous system. Compilers that support these standards can check for the presence of directives (also known as pragmas) in the source code, and generate parallel code for the specific regions annotated, so they can be run on a specified target device. DawnCC currently supports two standards, OpenACC and OpenMP, but it can easily be extended to support others. You can read more on the subject in the links below:

[OpenACC FAQ](http://www.openacc.org/faq-questions-inline)

[OpenACC MP](http://openmp.org/openmp-faq.html)

In order to use these standards to offload execution to accelerators, it is necessary to compile the modified source code with a compiler that supports the given directive standard (OpenMP 4.0 or OpenACC). In our internal testing environment, we use Portland Group's C Compiler for OpenACC support. You can find out more about it in the following link:

[Portland Group](http://www.pgroup.com/index.htm)

There are other compilers that provide support for OpenACC or OpenMP 4.0, either as fully-supported features or as experimental implementations. Below is a small list of such compilers:

[OpenMP Clang](http://openmp.llvm.org/) - The OpenMP runtime Clang implementation has been officially moved to an LLVM subproject. Currently supports offloading to accelerators using OpenMP 4.0 directives.

[GCC 5+](https://gcc.gnu.org/wiki/openmp) - Starting from version 5.0, GCC provides support for computation offloading through OpenMP 4.0 directives.

[Pathscale](http://www.pathscale.com/) - Pathscale's EKOPath compiler suite supposedly supports offloading with OpenMP 4.0+, as well as other annotation standards.

Note that, since most implementations are premiliminary and tend to change considerably, the annotation syntax inserted by DawnCC, while standard compliant, might not be fully compatible with each compiler's implementation. If you attempt to use a compiler that provides support for these standards but does not compile the annotation format DawnCC uses, we would appreciate knowing about it!

## Installation

The project is structured as a set of dynamically loaded libraries/passes for LLVM that can be built separately from the main compiler. However, an existing LLVM build (compiled using cmake) is necessary to build our code. 

You can download and build both LLVM/Clang and DawnCC using the following the [bash script](https://github.com/gleisonsdm/DawnCC-Compiler/blob/master/build.sh) on the folder you want the source to be downloaded and built. You will need CMake, wget, unzip, tar and a toolchain to run it.

Or you can build it manually by doing the following:

    Download [LLVM](http://llvm.org/releases/3.7.0/llvm-3.7.0.src.tar.xz) and [Clang](http://llvm.org/releases/3.7.0/cfe-3.7.0.src.tar.xz).

    Extract their contents to llvm and llvm/tools/clang.

    Download the DawnCC source and apply the patch "llvm-patch.diff" to your LLVM source directory, that is located in 'ArrayInference/llvm-patch.diff'.

    After applying the diff, we can move on to compiling a fresh LLVM+Clang 3.7 build. To do so, you can follow these outlines:

    	MAKEFLAG="-j8"
      
     	LLVM_SRC=<path-to-llvm-source-folder>
    	REPO=<path-to-dawncc-repository>

    	#We will build a debug version of LLVM+Clang under ${LLVM_SRC}/../llvm-build
    	mkdir ${LLVM_SRC}/../llvm-build
    	cd ${LLVM_SRC}/../llvm-build

    	#Setup clang plugins to be compiled alongside LLVM and Clang
    	${REPO}/src/ScopeFinder/setup.sh

    	#Create build setup for LLVM+Clang using CMake
    	cmake -DCMAKE_BUILD_TYPE=debug -DBUILD_SHARED_LIBS=ON ${LLVM_SRC}
    	
    	#Compile LLVM+Clang (this will likely take a while)
    	make ${MAKEFLAG}
    	cd -

    After you get a fresh LLVM build under ${LLVM_BUILD_DIR}, the following commands can be used to build DawnCC:

    	LLVM_BUILD_DIR=<path-to-llvm-build-folder> 	
    	REPO=<path-to-repository>

     	# Build the shared libraries under ${REPO}/lib, assumming an existing LLVM
     	# build under ${LLVM_BUILD_DIR}
     	mkdir ${REPO}/lib
     	cd ${REPO}/lib
     	cmake -DLLVM_DIR=${LLVM_BUILD_DIR}/share/llvm/cmake ../src/
     	make
    	cd -

## How to run a code

To run DawnCC, you can run the run.sh bash script, passing as arguments the directory which llvm-build and DawnCC are located and a directory containing source files to be processed (currently single level folder, not recursive). Arguments can be passed in command line to change behaviour of the script.
    
    ./run.sh -d <root folder> -src <folder with files to be processed> 

Or you can run DawnCC by copying and pasting the text below into a shell script file. You will have to change text between pointy brackets, e.g., *< like this >* to adapt the script to your environment.

 	LLVM_PATH="<root folder>/llvm-build/bin"

 	export CLANG="$LLVM_PATH/clang"
 	export CLANGFORM="$LLVM_PATH/clang-format"
 	export OPT="$LLVM_PATH/opt"

	export SCOPEFIND="$LLVM_PATH/../lib/scope-finder.so"

 	export BUILD=< DawnCC/lib >

 	export PRA="$BUILD/PtrRangeAnalysis/libLLVMPtrRangeAnalysis.so"
 	export AI="$BUILD/AliasInstrumentation/libLLVMAliasInstrumentation.so"
 	export DPLA="$BUILD/DepBasedParallelLoopAnalysis/libParallelLoopAnalysis.so"
 	export CP="$BUILD/CanParallelize/libCanParallelize.so"
 	export WAI="$BUILD/ArrayInference/libLLVMArrayInference.so"
 	export ST="$BUILD/ScopeTree/libLLVMScopeTree.so"

	export FLAGS="-mem2reg -tbaa -scoped-noalias -basicaa -functionattrs -gvn -loop-rotate
 	-instcombine -licm"
 	export FLAGSAI="-mem2reg -instnamer -loop-rotate"

 	rm result.bc result2.bc

 	$CLANGFORM -style="{BasedOnStyle: llvm, IndentWidth: 2}" -i < Source Code File(s) (.c/.cc/.cpp)>

 	$CLANG -Xclang -load -Xclang $SCOPEFIND -Xclang -add-plugin -Xclang -find-scope -g -O0 -c -fsyntax-only < Source Code File(s) (.c/.cc/.cpp)>

 	$CLANG -g -S -emit-llvm < Source Code > -o result.bc 

 	$OPT -load $PRA -load $AI -load $DPLA -load $CP $FLAGS -ptr-ra -basicaa \
 	  -scoped-noalias -alias-instrumentation -region-alias-checks \ 
 	  -can-parallelize -S result.bc

 	$OPT -load $ST -load $WAI -annotateParallel -S result.bc -o result2.bc

 	$OPT -S $FLAGSAI -load $ST -load $WAI -writeInFile -stats -Emit-GPU=< op1 > \
 	  -Emit-Parallel=< op2 > -Emit-OMP=< op3 > -Restrictifier=< op4 > \
 	  -Memory-Coalescing=< op5 > -Ptr-licm=< op6 > -Ptr-region=< op7 > \
	  -Run-Mode=false result2.bc -o result3.bc

 	$CLANGFORM -style="{BasedOnStyle: llvm, IndentWidth: 2}" -i < Source Code Files (.c/.cc/.cpp) >

Below, a summary of each part where it is necessary to change text:

- path-to-llvm-build-bin-folder : A reference to the location of the llvm-3.7 binaries. 

- DawnCC/lib : A reference to the location of the DawnCC libraries (.so files). 

- Source Code : The input file that will be used to run the analyses. 

- op1 => boolean that decides if the tool will analyze only the
  functions starting with "GPU__" or all functions in the source file. 
  
    true : Analyze only functions whose name begins with the "GPU__" prefix. 
    
    false : Analyze all functions.
    
- op2 => boolean that decides if the tool will analyze and annotate
  parallel loops. 
  
    true : Annotate loops as parallel, if possible. 
    
    false : Do not annotate loops as parallel. 
    
- op3 => Determines which annotation standard to use.

    2 : Annotate pragmas with OpenMP directives (CPU standard format). 
    
    1 : Annotate pragmas with OpenMP directives (GPU standard format). 
    
    0 : Annotate with default pragmas (OpenACC). 
    
- op4 => Determines if the tool should insert pointer disambiguation checks.

    true : Annotate tests. 
    
    false : Do not annotate tests. 
    
- op5 => Attempt to coalesce redundant memory copy directives. 

    true : Try to use regions to do the coalescing. 
    
    false : Do not use memory coalescing. 
    
- op6 => Try to use loop invariant code motion, to minimize aliasing. 

    true : Uses licm in Pointer Range Analysis. 
    
    false : Do not use Pointer Range Analysis with licm. 
    
- op7 => Attempt to reconstruct program regions, to find more coalescing opportunities.. 
  
    true : Try to rewrite regions. 
    
    false : Use only the regions available in LLVM IR. 





