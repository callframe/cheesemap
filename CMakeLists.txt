cmake_minimum_required(VERSION 4.0)
project(cheesemap
  VERSION 0.1.0
  DESCRIPTION "A high-performance HashMap"
)

if(MSVC)
  message(FATAL_ERROR "You need to have a GNU compatible compiler")
endif()

## configuration

set(CM_PANIC_SYM "panic_impl" CACHE STRING "Symbol name of the panic method")

option(CM_ENABLE_EXTENDED_DEBUG "Whether to enable extended debug information" OFF)
option(CM_ENABLE_UBSAN "Whether to enable undefined behavior sanitizer" OFF)
option(CM_ENABLE_ASAN "Whether to enable address sanitizer" OFF)
option(CM_ENABLE_SSE2 "Whether to enable SSE2 usage" OFF)

## source

set(cheesemap_source "${CMAKE_CURRENT_SOURCE_DIR}/cheesemap.c")

## library target

add_library(cheesemap OBJECT ${cheesemap_source})
target_include_directories(cheesemap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(cheesemap 
  PRIVATE 
    CM_PANIC_SYM=${CM_PANIC_SYM}
    $<$<BOOL:${CM_ENABLE_EXTENDED_DEBUG}>:CM_ENABLE_EXTENDED_DEBUG=1>
    $<$<BOOL:${CM_ENABLE_UBSAN}>:CM_ENABLE_UBSAN=1>
    $<$<BOOL:${CM_ENABLE_ASAN}>:CM_ENABLE_ASAN=1>
    $<$<BOOL:${CM_ENABLE_SSE2}>:CM_ENABLE_SSE2=1>
)

target_compile_options(cheesemap 
  PRIVATE 
    -Wall -Wextra -Werror
    -std=gnu11
    -fPIC

    $<$<BOOL:${CM_ENABLE_EXTENDED_DEBUG}>:-g3>
    $<$<BOOL:${CM_ENABLE_UBSAN}>:-fsanitize=undefined>
    $<$<BOOL:${CM_ENABLE_ASAN}>:-fsanitize=address>
    $<$<BOOL:${CM_ENABLE_SSE2}>:-msse2>
)

## configuration summary

message(STATUS "cheesemap configuration:")
message(STATUS "  CM_PANIC_SYM: ${CM_PANIC_SYM}")
message(STATUS "  CM_ENABLE_EXTENDED_DEBUG: ${CM_ENABLE_EXTENDED_DEBUG}")
message(STATUS "  CM_ENABLE_UBSAN: ${CM_ENABLE_UBSAN}")
message(STATUS "  CM_ENABLE_ASAN: ${CM_ENABLE_ASAN}")
message(STATUS "  CM_ENABLE_SSE2: ${CM_ENABLE_SSE2}")
