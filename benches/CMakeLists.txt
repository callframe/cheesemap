set(GBENCH_DIR "${BENCHES_DIR}/gbench")
set(CM_OLD_DIR "${BENCHES_DIR}/cheesemap_old")
set(ABSEIL_DIR "${BENCHES_DIR}/abseil")
set(KLIB "${BENCHES_DIR}/klib")

set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(${GBENCH_DIR})

add_subdirectory(${CM_OLD_DIR})
add_subdirectory(${ABSEIL_DIR})

function(add_competitive_bench target source)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE
        ${CM_DIR}
        ${BENCHES_DIR}
        ${KLIB}
    )
    target_link_libraries(${target} PRIVATE benchmark::benchmark_main)
endfunction()

add_competitive_bench(cheesemap_bench "${BENCHES_DIR}/cheesemap_bench.cc")

add_competitive_bench(cheesemap_old_bench "${BENCHES_DIR}/cheesemap_old_bench.cc")
target_link_libraries(cheesemap_old_bench PRIVATE cheesemap_old)

add_competitive_bench(abseil_bench "${BENCHES_DIR}/abseil_bench.cc")
target_link_libraries(abseil_bench PRIVATE absl::flat_hash_map)

add_competitive_bench(khash_bench "${BENCHES_DIR}/khash_bench.cc")

add_custom_target(bench
    DEPENDS
        cheesemap_bench
        cheesemap_old_bench
        abseil_bench
        khash_bench
)
