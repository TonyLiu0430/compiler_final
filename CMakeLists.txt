cmake_minimum_required(VERSION 3.25)
project(Compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_EXPERIMENTAL_REFLECTION "Enable experimental C++26 reflection support" OFF)
option(ENABLE_LLVM_CODEGEN "Build the LLVM code generator" ON)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h" "src/*.hpp")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
include_directories(src)
include_directories(include)
include_directories(include/third_party)

if(MSVC)
    add_compile_options("/utf-8")
endif()

add_executable(compiler ${SOURCES} ${HEADERS}) # add header to let vscode intellisense work well

if(ENABLE_LLVM_CODEGEN)
    set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "" FORCE)
    set(LLVM_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLVM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLVM_INCLUDE_UTILS OFF CACHE BOOL "" FORCE)
    set(LLVM_BUILD_UTILS OFF CACHE BOOL "" FORCE)
    set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
    set(LLVM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(LLVM_ENABLE_BINDINGS OFF CACHE BOOL "" FORCE)
    set(LLVM_ENABLE_ZLIB OFF CACHE STRING "" FORCE)
    set(LLVM_ENABLE_ZSTD OFF CACHE STRING "" FORCE)
    set(LLVM_ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
    set(LLVM_ENABLE_TERMINFO OFF CACHE BOOL "" FORCE)

    set(C9AY_CXX_STANDARD ${CMAKE_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD 17)
    add_subdirectory(
        third_party/llvm-project/llvm
        ${CMAKE_BINARY_DIR}/llvm
        EXCLUDE_FROM_ALL
    )
    set(CMAKE_CXX_STANDARD ${C9AY_CXX_STANDARD})

    llvm_map_components_to_libnames(C9AY_LLVM_LIBS
        Core
        Support
    )

    add_library(c9ay_llvm INTERFACE)
    target_include_directories(c9ay_llvm SYSTEM INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llvm-project/llvm/include
        ${CMAKE_BINARY_DIR}/llvm/include
    )
    target_compile_definitions(c9ay_llvm INTERFACE
        ${LLVM_DEFINITIONS}
    )
    target_link_libraries(c9ay_llvm INTERFACE
        ${C9AY_LLVM_LIBS}
    )

    target_link_libraries(compiler PRIVATE c9ay_llvm)
endif()

if(ENABLE_EXPERIMENTAL_REFLECTION)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(compiler PRIVATE -freflection)
    else()
        message(FATAL_ERROR "ENABLE_EXPERIMENTAL_REFLECTION currently requires GCC with -freflection support")
    endif()
endif()

if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(compiler PRIVATE stdc++exp)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(compiler_tests tests/parser_test.cpp)

    if(ENABLE_LLVM_CODEGEN)
        target_link_libraries(compiler_tests PRIVATE c9ay_llvm)
    endif()

    if(ENABLE_EXPERIMENTAL_REFLECTION)
        target_compile_options(compiler_tests PRIVATE -freflection)
    endif()

    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(compiler_tests PRIVATE stdc++exp)
    endif()

    add_test(NAME compiler_tests COMMAND compiler_tests)
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_tests_properties(compiler_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:C:/dev/winlibs-x86_64-posix-seh-gcc-16.0.1-snapshot20260222-mingw-w64ucrt-13.0.0-r1/mingw64/bin"
        )
    endif()
endif()
