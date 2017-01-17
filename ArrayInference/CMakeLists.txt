cmake_minimum_required(VERSION 2.8)

add_subdirectory(Coalescing)

add_library(LLVMArrayInference MODULE
  writeInFile.cpp
  writeExpressions.cpp
  recoverCode.cpp
  recoverNames.cpp
  restrictifier.cpp
  constantsSimplify.cpp
  PtrRangeAnalysis.cpp
  SCEVRangeBuilder.cpp  
  annotateLoopParallel.cpp
  regionReconstructor.cpp
  recoverExpressions.cpp
)

