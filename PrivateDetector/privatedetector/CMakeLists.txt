cmake_minimum_required(VERSION 2.8)

add_library(CLANGPrivateDetector MODULE
	privatedetector.cpp
)

if(LLVM_ENABLE_PLUGINS AND (WIN32 OR CYGWIN))
  target_link_libraries(ompextractor ${cmake_2_8_12_PRIVATE}
    clangAST
    clangBasic
    clangFrontend
    LLVMSupport
    )
endif()
