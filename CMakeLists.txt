cmake_minimum_required(VERSION 3.25)
project(Compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_EXPERIMENTAL_REFLECTION "Enable experimental C++26 reflection support" OFF)
option(ENABLE_LLVM_CODEGEN "Build the LLVM code generator" ON)
option(C9AY_BUILD_LLVM "Build LLVM inside this project (one-time bootstrap)" OFF)
option(C9AY_STATIC_MINGW_RUNTIME "Link libgcc and winpthreads statically when using MinGW" ON)
option(C9AY_STATIC_LIBSTDCXX "Experimentally link libstdc++ statically when using MinGW" OFF)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "src/*.h"
    "src/*.hpp"
    "src/*.inl"
)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
include_directories(src)
include_directories(include)
include_directories(include/third_party)

if(MSVC)
    add_compile_options("/utf-8")
endif()

add_executable(compiler ${SOURCES} ${HEADERS}) # add header to let vscode intellisense work well

set(C9AY_RUNTIME_DIRECTORY "${CMAKE_BINARY_DIR}/runtime")
set(C9AY_RUNTIME_OBJECT
    "${C9AY_RUNTIME_DIRECTORY}/c9ay_runtime.o"
)
file(TO_CMAKE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    C9AY_INCLUDE_DIRECTORY_DEFINE
)
file(TO_CMAKE_PATH
    "${C9AY_RUNTIME_OBJECT}"
    C9AY_RUNTIME_OBJECT_DEFINE
)
target_compile_definitions(compiler PRIVATE
    C9AY_INCLUDE_DIR="${C9AY_INCLUDE_DIRECTORY_DEFINE}"
    C9AY_RUNTIME_OBJECT="${C9AY_RUNTIME_OBJECT_DEFINE}"
)

if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(C9AY_MINGW_BIN_DIRECTORY
        "${CMAKE_CXX_COMPILER}"
        DIRECTORY
    )
    get_filename_component(C9AY_MINGW_ROOT
        "${C9AY_MINGW_BIN_DIRECTORY}"
        DIRECTORY
    )
    set(C9AY_MINGW_LIBRARY_DIRECTORY
        "${C9AY_MINGW_ROOT}/x86_64-w64-mingw32/lib"
    )
    file(TO_CMAKE_PATH
        "${C9AY_MINGW_BIN_DIRECTORY}"
        C9AY_MINGW_BIN_DIRECTORY_DEFINE
    )
    file(TO_CMAKE_PATH
        "${C9AY_MINGW_LIBRARY_DIRECTORY}"
        C9AY_MINGW_LIBRARY_DIRECTORY_DEFINE
    )
    target_compile_definitions(compiler PRIVATE
        C9AY_MINGW_BIN_DIR="${C9AY_MINGW_BIN_DIRECTORY_DEFINE}"
        C9AY_MINGW_LIBRARY_DIR="${C9AY_MINGW_LIBRARY_DIRECTORY_DEFINE}"
    )
endif()

if(ENABLE_LLVM_CODEGEN)
    set(C9AY_LLVM_DIR
        "${CMAKE_BINARY_DIR}/llvm/lib/cmake/llvm"
        CACHE PATH
        "Directory containing the prebuilt LLVMConfig.cmake"
    )

    if(C9AY_BUILD_LLVM)
        message(STATUS "LLVM bootstrap mode is enabled")
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
        set(C9AY_LLVM_INCLUDE_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llvm-project/llvm/include
            ${CMAKE_BINARY_DIR}/llvm/include
        )
    else()
        if(NOT EXISTS "${C9AY_LLVM_DIR}/LLVMConfig.cmake")
            message(FATAL_ERROR
                "Prebuilt LLVM was not found at:\n"
                "  ${C9AY_LLVM_DIR}\n"
                "Configure once with C9AY_BUILD_LLVM=ON."
            )
        endif()

        find_package(LLVM REQUIRED CONFIG
            PATHS "${C9AY_LLVM_DIR}"
            NO_DEFAULT_PATH
        )
        message(STATUS
            "Using prebuilt LLVM ${LLVM_PACKAGE_VERSION}: "
            "${LLVM_BUILD_BINARY_DIR}"
        )

        set(C9AY_REQUIRED_LLVM_ARCHIVES
            "${LLVM_LIBRARY_DIR}/libLLVMX86CodeGen.a"
            "${LLVM_LIBRARY_DIR}/libLLVMX86Desc.a"
            "${LLVM_LIBRARY_DIR}/libLLVMX86Info.a"
        )
        set(C9AY_MISSING_LLVM_ARCHIVES)
        foreach(archive IN LISTS C9AY_REQUIRED_LLVM_ARCHIVES)
            if(NOT EXISTS "${archive}")
                list(APPEND C9AY_MISSING_LLVM_ARCHIVES "${archive}")
            endif()
        endforeach()
        if(C9AY_MISSING_LLVM_ARCHIVES)
            list(JOIN C9AY_MISSING_LLVM_ARCHIVES "\n  " missing_archives)
            message(FATAL_ERROR
                "The prebuilt LLVM tree is missing the X86 backend:\n"
                "  ${missing_archives}\n"
                "Use the gcc16-cxx26-llvm preset once."
            )
        endif()
        set(C9AY_LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS})
    endif()

    llvm_map_components_to_libnames(C9AY_LLVM_LIBS
        Core
        Support
        Target
        CodeGen
        Analysis
        TransformUtils
        InstCombine
        ScalarOpts
        MC
        AsmPrinter
        X86CodeGen
        X86Desc
        X86Info
    )

    add_library(c9ay_llvm INTERFACE)
    target_include_directories(c9ay_llvm SYSTEM INTERFACE
        ${C9AY_LLVM_INCLUDE_DIRS}
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

function(c9ay_apply_mingw_link_options target)
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(${target} PRIVATE stdc++exp)
        target_link_options(${target} PRIVATE -s)

        if(C9AY_STATIC_MINGW_RUNTIME)
            # Keep LLVM/component libraries linked in their configured mode.
            # Do not use a global "-static" here.
            target_link_options(${target} PRIVATE
                -static-libgcc
                -Wl,-Bstatic
                -lwinpthread
                -Wl,-Bdynamic
            )
        endif()

        if(C9AY_STATIC_LIBSTDCXX)
            target_link_options(${target} PRIVATE -static-libstdc++)
        endif()
    endif()
endfunction()

function(c9ay_copy_mingw_runtime_dlls target)
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        foreach(dll IN ITEMS
            libstdc++-6.dll
            libgcc_s_seh-1.dll
            libwinpthread-1.dll
        )
            set(dll_path "${C9AY_MINGW_BIN_DIRECTORY}/${dll}")
            if(EXISTS "${dll_path}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${dll_path}"
                        "$<TARGET_FILE_DIR:${target}>/${dll}"
                    VERBATIM
                )
            endif()
        endforeach()
    endif()
endfunction()

if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    c9ay_apply_mingw_link_options(compiler)
    c9ay_copy_mingw_runtime_dlls(compiler)
endif()

add_custom_command(
    OUTPUT "${C9AY_RUNTIME_OBJECT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${C9AY_RUNTIME_DIRECTORY}"
    COMMAND $<TARGET_FILE:compiler>
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/c9ay_runtime.c"
        -c
        -o
        "${C9AY_RUNTIME_OBJECT}"
    DEPENDS
        compiler
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/c9ay_runtime.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/c9ay.h"
    COMMENT
        "Building c9ay runtime with the c9ay compiler"
    VERBATIM
)
add_custom_target(c9ay_runtime ALL
    DEPENDS "${C9AY_RUNTIME_OBJECT}"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(compiler_tests
        tests/parser_test.cpp
        tests/fixture_test.cpp
    )
    add_dependencies(compiler_tests c9ay_runtime)
    target_compile_definitions(compiler_tests PRIVATE
        C9AY_INCLUDE_DIR="${C9AY_INCLUDE_DIRECTORY_DEFINE}"
        C9AY_RUNTIME_OBJECT="${C9AY_RUNTIME_OBJECT_DEFINE}"
        C9AY_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/test"
    )
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_definitions(compiler_tests PRIVATE
            C9AY_MINGW_BIN_DIR="${C9AY_MINGW_BIN_DIRECTORY_DEFINE}"
            C9AY_MINGW_LIBRARY_DIR="${C9AY_MINGW_LIBRARY_DIRECTORY_DEFINE}"
        )
    endif()

    if(ENABLE_LLVM_CODEGEN)
        target_link_libraries(compiler_tests PRIVATE c9ay_llvm)
    endif()

    if(ENABLE_EXPERIMENTAL_REFLECTION)
        target_compile_options(compiler_tests PRIVATE -freflection)
    endif()

    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(compiler_tests PRIVATE -Wa,-mbig-obj)
        c9ay_apply_mingw_link_options(compiler_tests)
        c9ay_copy_mingw_runtime_dlls(compiler_tests)
    endif()

    add_test(NAME compiler_tests COMMAND compiler_tests)
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_tests_properties(compiler_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:C:/dev/winlibs-x86_64-posix-seh-gcc-16.0.1-snapshot20260222-mingw-w64ucrt-13.0.0-r1/mingw64/bin"
        )
    endif()
endif()
