cmake_minimum_required(VERSION 4.0)
project(cheesemap
  VERSION 0.2.0
  DESCRIPTION "A high-performance HashMap"
  LANGUAGES C CXX
)

if(MSVC)
  message(FATAL_ERROR "You need to have a GNU compatible compiler")
endif()

## configuration

set(CM_PANIC_SYM "panic_impl" CACHE STRING "Symbol name of the panic method")

option(CM_ENABLE_UBSAN "Whether to enable undefined behavior sanitizer" OFF)
option(CM_ENABLE_ASAN "Whether to enable address sanitizer" OFF)
option(CM_ENABLE_SSE2 "Whether to enable SSE2 usage" OFF)
option(CM_ENABLE_NATIVE "Whether to enable -march=native" OFF)
option(CM_ENABLE_BENCHES "Whether to build the benchmarks" OFF)

## source

set(cm_topdir "${CMAKE_CURRENT_SOURCE_DIR}")
set(cm_benchesdir "${cm_topdir}/benches")
set(cm_source "${cm_topdir}/cheesemap.c")

## library target

add_library(cheesemap OBJECT ${cm_source})
target_include_directories(cheesemap PUBLIC ${cm_topdir})

target_compile_definitions(cheesemap 
  PRIVATE 
    CM_PANIC_SYM=${CM_PANIC_SYM}
    $<$<BOOL:${CM_ENABLE_UBSAN}>:CM_ENABLE_UBSAN=1>
    $<$<BOOL:${CM_ENABLE_ASAN}>:CM_ENABLE_ASAN=1>
    $<$<BOOL:${CM_ENABLE_SSE2}>:CM_ENABLE_SSE2=1>
)

target_compile_options(cheesemap
  PRIVATE
    -Wall -Wextra -Werror
    $<$<CONFIG:Debug>:-g3>
    $<$<BOOL:${CM_ENABLE_UBSAN}>:-fsanitize=undefined>
    $<$<BOOL:${CM_ENABLE_ASAN}>:-fsanitize=address>
    $<$<BOOL:${CM_ENABLE_SSE2}>:-msse2>
    $<$<BOOL:${CM_ENABLE_NATIVE}>:-march=native>
)

set_target_properties(cheesemap PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS ON
  POSITION_INDEPENDENT_CODE ON
)

## Benches
if(CM_ENABLE_BENCHES)
  add_subdirectory("${cm_benchesdir}")
endif()

## configuration summary

message(STATUS "cheesemap configuration:")
message(STATUS "  CM_PANIC_SYM: ${CM_PANIC_SYM}")
message(STATUS "  CM_ENABLE_UBSAN: ${CM_ENABLE_UBSAN}")
message(STATUS "  CM_ENABLE_ASAN: ${CM_ENABLE_ASAN}")
message(STATUS "  CM_ENABLE_SSE2: ${CM_ENABLE_SSE2}")
message(STATUS "  CM_ENABLE_NATIVE: ${CM_ENABLE_NATIVE}")
message(STATUS "  CM_ENABLE_BENCHES: ${CM_ENABLE_BENCHES}")
