cmake_minimum_required(VERSION 2.8)

find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

### set compilers
if("${LLVM_TOOLS_BINARY_DIR}" STREQUAL "")
  message(FATAL_ERROR "\${LLVM_TOOLS_BINARY_DIR} was not set, try setting CMAKE_PREFIX_PATH")
endif()

find_program(CLANG   clang   NO_DEFAULT_PATH PATHS "${LLVM_TOOLS_BINARY_DIR}")
find_program(CLANGXX clang++ NO_DEFAULT_PATH PATHS "${LLVM_TOOLS_BINARY_DIR}")

if(("${CLANG}" STREQUAL "CLANG-NOTFOUND") OR ("${CLANGXX}" STREQUAL "CLANGXX-NOTFOUND"))
  message(FATAL_ERROR "Could not find clang, try setting CMAKE_PREFIX_PATH")
endif()

### add some C++ compiler flags
## unfortunately we can't use add_compile_options here since that would also add them to C files
set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")      ## set C++ standard to use
set(CMAKE_CXX_FLAGS "-fno-rtti ${CMAKE_CXX_FLAGS}")       ## disable C++ RTTI
set(CMAKE_CXX_FLAGS "-fno-exceptions ${CMAKE_CXX_FLAGS}") ## disable C++ exceptions

# If LLVM was build in debug mode, compile with debug info as well
if ("${LLVM_ENABLE_ASSERTIONS}" STREQUAL "ON")
  set(CMAKE_C_FLAGS   "-g ${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "-g ${CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_C_FLAGS "-DNDEBUG ${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "-DNDEBUG ${CMAKE_CXX_FLAGS}")
endif()

## make sure clang prints fancy coloured output when run from make
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  set(CMAKE_C_FLAGS   "-fcolor-diagnostics ${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "-fcolor-diagnostics ${CMAKE_CXX_FLAGS}")
endif()

add_subdirectory(ArrayInference)
add_subdirectory(PtrRangeAnalysis)
add_subdirectory(AliasInstrumentation)
add_subdirectory(DepBasedParallelLoopAnalysis)
add_subdirectory(CanParallelize)
add_subdirectory(ParallelLoopMetadata)
add_subdirectory(ScopeTree)
