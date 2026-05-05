cmake_minimum_required(VERSION 3.10)
project(thread_utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options("-O3")

# Library sources (exclude main.cpp)
set(LIB_SRCS
    src/thread_utils/dfg/dfg.cpp
    src/thread_utils/dfg/graph.cpp
    src/thread_utils/tools/thread_pool.cpp
    src/thread_utils/tools/async_worker.cpp
    src/thread_utils/tools/profiling.cpp
    src/thread_utils/task_manager/task_manager.cpp
)

add_library(thread_utils ${LIB_SRCS})
target_include_directories(thread_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(tests)
