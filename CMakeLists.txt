cmake_minimum_required(VERSION 3.10)

set(CM "cheesemap")
project(${CM}
    VERSION 0.1.0
    DESCRIPTION "A high-performance hash map implementation in C"
    LANGUAGES CXX
)

option(CM_BUILD_BENCHES "Build benchmarks" ON)

set(CM_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CM_SOURCE "${CM_DIR}/cheesemap.cc")

add_library(${CM} INTERFACE ${CM_SOURCE})

target_include_directories(${CM} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${CM} INTERFACE std_cxx_17)

if(CM_BUILD_BENCHES)
    set(BENCHES_DIR "${CM_DIR}/benches")
    add_subdirectory(${BENCHES_DIR})
endif()