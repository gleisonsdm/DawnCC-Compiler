Project Dawn: Array Inference.
Authors:
  Pericles Alves [periclesrafael@dcc.ufmg.br]
  Gleison Souza [gleison14051994@gmail.com.br]

This directory contains the source code for the Array Inference tool.

-------------------------------------
         BUILDING THE CODE
-------------------------------------
The project is structured as a set of dynamically loaded libraries/passes for
LLVM that can be built separate from the main compiler. However, an existing
LLVM build (compiled using cmake) is necessary to build our code. The base LLVM
version used in this project was LLVM 3.7 release:

  - LLVM: http://llvm.org/releases/3.7.0/llvm-3.7.0.src.tar.xz
  - Clang: http://llvm.org/releases/3.7.0/cfe-3.7.0.src.tar.xz

To use Array Inference, please, read project Dawn's README.txt

-------------------------------------
        RUNNING SIMPLE TESTS
-------------------------------------
To run the hybrid version of our code, simply compile llvm bytecode for 
your program using Clang, then load our dynamic libraries using llvm's opt.

Please, change these flags:
  -> LIBR => directory where the ArrayInference dynamic library is located.
  -> BENCH_DIR => Benchmark's directory.
  -> BENCH => Benchmark's file name.
  -> OPTION1 => boolean that decides if the tool will be analyze just the
  functions starting with "GPU__" or all functions in the source file.
    true : Analyze all functions.
    false : Just Analyze the functions starting with "GPU__".
  -> OPTION2 => boolean that decides if the tool will analyze and annotate
  parallel loops.
    true : Annotate loops as parallel, if possible.
    false : Do not annotate loops as parallel.
  -> OPTION3 => Generates all pragmas in OpenMP.
    2 : Annotate pragmas with OpenMP directives (CPU standard format).
    1 : Annotate pragmas with OpenMP directives (GPU standard format).
    0 : Annotate with default pragmas (OpenACC).
  -> OPTION4 => Write tests to desambiguate pointers.
    true : Annotate tests.
    false : Do not annotate tests.
  -> OPTION5 => Using an file with another analysis's results to annotate
  and identify parallel loops.
    **** : A string with the file location.
  -> OPTION6 => Desconsider annotation when a loops has divergent instruction.
    true : Uses divergent's analysis to identify divergent instructions.
    false : Ignore divergent's analysis.
  -> OPTION7 => Try to do memory coalescing to avoid data transference.
    true : Try to use regions to do the coalescing.
    false : Do not use memory coalesing.
  -> OPTION8 => Try to use loop invariant code motion, to avoid alias impact.
  true : Uses licm in Pointer Range Analysis, case necessary.
  false : Do not use Pointer Range Analysis licm.
  -> OPTION9 => Try to rewrite regions, and analyze a new region define with
  an analysis.
  true : Try rewrite regions.
  false : Use just the regions available in the IR.
  -> OPTION10 => Define if the annotation will be parallel loops or tasks.
  true : Annotate parallel loops.
  false : Annotate tasks (OpenMP only).

# Run Clang and opt loading our dynamic libraries
  ./clang -g -O0 -c -emit-llvm ${BENCH_DIR}/$BENCH.c -o ${BENCH_DIR}/$BENCH.bc

  ./opt -mem2reg -instnamer -loop-rotate \
  -load ${LIBR}/libLLVMArrayInference.so \
    -writeInFile -Emit-GPU=$OPTION1 -Emit-Parallel=$OPTION2 \
    -Emit-OMP=$OPTION3 -Restrictifier=$OPTION4 \
    -Parallel-File=$OPTION5 -Discard-Divergent=$OPTION6 \
    -Memory-Coalescing=$OPTION67 -Ptr-licm=$OPTION8 \
    -Ptr-region=$OPTION9 -Run-Mode=$OPTION10 \ 
    ${BENCH_DIR}/$BENCH.bc
