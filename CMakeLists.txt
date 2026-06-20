cmake_minimum_required(VERSION 3.25)
project(Compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_EXPERIMENTAL_REFLECTION "Enable experimental C++26 reflection support" OFF)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h" "src/*.hpp")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
include_directories(src)
include_directories(include)
include_directories(include/third_party)

if(MSVC)
    add_compile_options("/utf-8")
endif()

add_executable(compiler ${SOURCES} ${HEADERS}) # add header to let vscode intellisense work well

if(ENABLE_EXPERIMENTAL_REFLECTION)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(compiler PRIVATE -freflection)
    else()
        message(FATAL_ERROR "ENABLE_EXPERIMENTAL_REFLECTION currently requires GCC with -freflection support")
    endif()
endif()
