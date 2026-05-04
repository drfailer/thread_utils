cmake_minimum_required(VERSION 3.10)
project(thread_utils_tests LANGUAGES CXX)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dgemm)
include_directories(${CMAKE_SOURCE_DIR}/../hedgehog/hedgehog)

add_compile_options("-O3")

find_library(TBB tbb)
if(NOT TBB)
    set(TBB "")
endif()

add_executable(dgemm dgemm/dgemm.cpp common/matrix.cpp)
target_link_libraries(dgemm PRIVATE thread_utils openblas ${TBB} atomic)

add_executable(hadamard hadamard/hadamard.cpp common/matrix.cpp)
target_link_libraries(hadamard PRIVATE thread_utils atomic)
