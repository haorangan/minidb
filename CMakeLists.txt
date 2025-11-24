cmake_minimum_required(VERSION 3.16)
project(MiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to an optimized build when no build type is given (IDEs set their own)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

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

add_executable(minidb_tests
        ${CMAKE_SOURCE_DIR}/tests/bplus_tree_test.cpp
)
target_link_libraries(minidb_tests PRIVATE minidb_lib)

add_executable(minidb_query_tests
        ${CMAKE_SOURCE_DIR}/tests/query_test.cpp
)
target_link_libraries(minidb_query_tests PRIVATE minidb_lib)

add_executable(minidb_crash_tests
        ${CMAKE_SOURCE_DIR}/tests/crash_test.cpp
)
target_link_libraries(minidb_crash_tests PRIVATE minidb_lib)

add_executable(minidb_bench
        ${CMAKE_SOURCE_DIR}/bench/point_query_bench.cpp
)
target_link_libraries(minidb_bench PRIVATE minidb_lib)

enable_testing()
add_test(NAME bplus_tree_test COMMAND minidb_tests)
add_test(NAME query_test COMMAND minidb_query_tests)
add_test(NAME crash_test COMMAND minidb_crash_tests)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR
        CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(minidb_lib PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(minidb_demo PRIVATE -Wall -Wextra -Wpedantic)
elseif (MSVC)
    target_compile_options(minidb_lib PRIVATE /W4)
    target_compile_options(minidb_demo PRIVATE /W4)
endif()