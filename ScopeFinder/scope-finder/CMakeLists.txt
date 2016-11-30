# If we don't need RTTI or EH, there's no reason to export anything
# from the plugin.
if( NOT MSVC ) # MSVC mangles symbols differently, and
                # scope-finder.exports may contain C++ symbols.
  if( NOT LLVM_REQUIRES_RTTI )
    if( NOT LLVM_REQUIRES_EH )
      set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/scope-finder.exports)
    endif()
  endif()
endif()

add_llvm_loadable_module(scope-finder scope-finder.cpp)

if(LLVM_ENABLE_PLUGINS AND (WIN32 OR CYGWIN))
  target_link_libraries(scope-finder ${cmake_2_8_12_PRIVATE}
    clangAST
    clangBasic
    clangFrontend
    LLVMSupport
    )
endif()
