cmake_minimum_required(VERSION 2.8)

add_library(LLVMPtrRangeAnalysis MODULE
  PtrRangeAnalysis.cpp
  SCEVRangeBuilder.cpp
)
