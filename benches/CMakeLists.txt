set(GBENCH_DIR "${BENCHES_DIR}/gbench")
set(CM_OLD_DIR "${BENCHES_DIR}/cheesemap_old")
set(ABSEIL_DIR "${BENCHES_DIR}/abseil")
set(KLIB "${BENCHES_DIR}/klib")

set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(${GBENCH_DIR})

add_subdirectory(${CM_OLD_DIR})
add_subdirectory(${ABSEIL_DIR})