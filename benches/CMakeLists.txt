set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_USE_BUNDLED_GTEST OFF CACHE BOOL "" FORCE)

add_subdirectory("${cm_benchesdir}/gbench" EXCLUDE_FROM_ALL)

set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${cm_benchesdir}/abseil" EXCLUDE_FROM_ALL)

set(cm_bench_targets
  cheesemap_bench
  tidwall_bench
  unordered_map_bench
  abseil_bench
  klib_bench
)

foreach(target_name IN LISTS cm_bench_targets)
  add_executable(${target_name} "${cm_benchesdir}/${target_name}.cc")

  target_include_directories(${target_name}
    PRIVATE
      "${cm_topdir}"
      "${cm_benchesdir}"
  )

  target_compile_options(${target_name}
    PRIVATE
      -Wall -Wextra -Werror
      $<$<BOOL:${CM_ENABLE_NATIVE}>:-march=native>
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target_name} PRIVATE -O3)
  endif()

  target_link_libraries(${target_name}
    PRIVATE
      benchmark::benchmark_main
  )

  set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
  )
endforeach()

target_link_libraries(cheesemap_bench PRIVATE cheesemap)

set(cm_tidwalldir "${cm_benchesdir}/tidwall")
target_include_directories(tidwall_bench PRIVATE "${cm_tidwalldir}")
target_sources(tidwall_bench PRIVATE "${cm_tidwalldir}/hashmap.c")

target_include_directories(abseil_bench PRIVATE "${cm_benchesdir}/abseil")
target_link_libraries(abseil_bench PRIVATE absl::flat_hash_map)

set(cm_klibdir "${cm_benchesdir}/klib")
target_include_directories(klib_bench PRIVATE "${cm_klibdir}")
