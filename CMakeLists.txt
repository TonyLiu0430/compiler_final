cmake_minimum_required(VERSION 3.20)
project(Compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h" "src/*.hpp")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
include_directories(src)
include_directories(include)
include_directories(include/third_party)

if(MSVC)
    add_compile_options("/utf-8")
endif()

add_executable(compiler ${SOURCES} ${HEADERS}) # add header to let vscode intellisense work well
