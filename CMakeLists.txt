cmake_minimum_required(VERSION 3.16)
project(MiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Collect all source files
file(GLOB_RECURSE MINIDB_SOURCES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM MINIDB_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

add_library(minidb_lib STATIC
        ${MINIDB_SOURCES}
)

add_executable(minidb_demo
        ${CMAKE_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(minidb_demo
        PRIVATE minidb_lib
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR
        CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(minidb_lib PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(minidb_demo PRIVATE -Wall -Wextra -Wpedantic)
elseif (MSVC)
    target_compile_options(minidb_lib PRIVATE /W4)
    target_compile_options(minidb_demo PRIVATE /W4)
endif()