cmake_minimum_required(VERSION 2.8)

add_library(LLVMAliasInstrumentation MODULE
  AliasInstrumentation.cpp
  RegionCloneUtil.cpp
)
