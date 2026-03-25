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

set(cheesemap_source "${CMAKE_SOURCE_DIR}/cheesemap.c")

## helper functions

function(add_if_enabled target option_name)
  if(NOT ${option_name})
    return()
  endif()

  cmake_parse_arguments(ARG "" "" "COMPILE_OPTIONS;LINK_OPTIONS;DEFINITIONS" ${ARGN})

  if(ARG_COMPILE_OPTIONS)
    target_compile_options(${target} PRIVATE ${ARG_COMPILE_OPTIONS})
  endif()

  if(ARG_LINK_OPTIONS)
    target_link_options(${target} PRIVATE ${ARG_LINK_OPTIONS})
  endif()

  if(ARG_DEFINITIONS)
    target_compile_definitions(${target} PRIVATE ${ARG_DEFINITIONS})
  endif()
endfunction()

## library target

add_library(cheesemap OBJECT ${cheesemap_source})

set_target_properties(cheesemap PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS ON
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(cheesemap PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(cheesemap PRIVATE CM_PANIC_SYM=${CM_PANIC_SYM})
target_compile_options(cheesemap PRIVATE -Wall -Wextra -Werror)

add_if_enabled(cheesemap CM_ENABLE_EXTENDED_DEBUG COMPILE_OPTIONS -g3)
add_if_enabled(cheesemap CM_ENABLE_UBSAN COMPILE_OPTIONS -fsanitize=undefined LINK_OPTIONS -fsanitize=undefined)
add_if_enabled(cheesemap CM_ENABLE_ASAN COMPILE_OPTIONS -fsanitize=address LINK_OPTIONS -fsanitize=address)
add_if_enabled(cheesemap CM_ENABLE_SSE2 COMPILE_OPTIONS -msse2 DEFINITIONS CM_ENABLE_SSE2=1)


