#include <benchmark/benchmark.h>

#include "bench_common.h"

namespace {

using namespace cmbench;

static void BM_Insert_Scalar(benchmark::State& state) {
  bench_insert<AbseilAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(),
      {"absl::flat_hash_map", "c++", "Scalar", "Insert"});
}
static void BM_LookupHit_Scalar(benchmark::State& state) {
  bench_lookup_hit<AbseilAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(),
      {"absl::flat_hash_map", "c++", "Scalar", "LookupHit"});
}
static void BM_LookupMiss_Scalar(benchmark::State& state) {
  bench_lookup_miss<AbseilAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(),
      {"absl::flat_hash_map", "c++", "Scalar", "LookupMiss"});
}
static void BM_Erase_Scalar(benchmark::State& state) {
  bench_erase<AbseilAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(),
      {"absl::flat_hash_map", "c++", "Scalar", "Erase"});
}

static void BM_Insert_HandlePayload(benchmark::State& state) {
  bench_insert<AbseilAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"absl::flat_hash_map", "c++", "HandlePayload", "Insert"});
}
static void BM_LookupHit_HandlePayload(benchmark::State& state) {
  bench_lookup_hit<AbseilAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"absl::flat_hash_map", "c++", "HandlePayload", "LookupHit"});
}
static void BM_LookupMiss_HandlePayload(benchmark::State& state) {
  bench_lookup_miss<AbseilAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"absl::flat_hash_map", "c++", "HandlePayload", "LookupMiss"});
}
static void BM_Erase_HandlePayload(benchmark::State& state) {
  bench_erase<AbseilAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"absl::flat_hash_map", "c++", "HandlePayload", "Erase"});
}

static void BM_Insert_CompositeKey(benchmark::State& state) {
  bench_insert<AbseilAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"absl::flat_hash_map", "c++", "CompositeKey", "Insert"});
}
static void BM_LookupHit_CompositeKey(benchmark::State& state) {
  bench_lookup_hit<AbseilAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"absl::flat_hash_map", "c++", "CompositeKey", "LookupHit"});
}
static void BM_LookupMiss_CompositeKey(benchmark::State& state) {
  bench_lookup_miss<AbseilAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"absl::flat_hash_map", "c++", "CompositeKey", "LookupMiss"});
}
static void BM_Erase_CompositeKey(benchmark::State& state) {
  bench_erase<AbseilAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"absl::flat_hash_map", "c++", "CompositeKey", "Erase"});
}

}  // namespace

BENCHMARK(BM_Insert_Scalar);
BENCHMARK(BM_LookupHit_Scalar);
BENCHMARK(BM_LookupMiss_Scalar);
BENCHMARK(BM_Erase_Scalar);

BENCHMARK(BM_Insert_HandlePayload);
BENCHMARK(BM_LookupHit_HandlePayload);
BENCHMARK(BM_LookupMiss_HandlePayload);
BENCHMARK(BM_Erase_HandlePayload);

BENCHMARK(BM_Insert_CompositeKey);
BENCHMARK(BM_LookupHit_CompositeKey);
BENCHMARK(BM_LookupMiss_CompositeKey);
BENCHMARK(BM_Erase_CompositeKey);
