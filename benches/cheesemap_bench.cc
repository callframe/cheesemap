#include <benchmark/benchmark.h>

#include "bench_common.h"

namespace {

using namespace cmbench;

static void BM_Insert_Scalar(benchmark::State& state) {
  bench_insert<CheesemapAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(), {"cheesemap", "c", "Scalar", "Insert"});
}
static void BM_LookupHit_Scalar(benchmark::State& state) {
  bench_lookup_hit<CheesemapAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(), {"cheesemap", "c", "Scalar", "LookupHit"});
}
static void BM_LookupMiss_Scalar(benchmark::State& state) {
  bench_lookup_miss<CheesemapAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(), {"cheesemap", "c", "Scalar", "LookupMiss"});
}
static void BM_Erase_Scalar(benchmark::State& state) {
  bench_erase<CheesemapAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(), {"cheesemap", "c", "Scalar", "Erase"});
}

static void BM_Insert_HandlePayload(benchmark::State& state) {
  bench_insert<CheesemapAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(), {"cheesemap", "c", "HandlePayload", "Insert"});
}
static void BM_LookupHit_HandlePayload(benchmark::State& state) {
  bench_lookup_hit<CheesemapAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"cheesemap", "c", "HandlePayload", "LookupHit"});
}
static void BM_LookupMiss_HandlePayload(benchmark::State& state) {
  bench_lookup_miss<CheesemapAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"cheesemap", "c", "HandlePayload", "LookupMiss"});
}
static void BM_Erase_HandlePayload(benchmark::State& state) {
  bench_erase<CheesemapAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(), {"cheesemap", "c", "HandlePayload", "Erase"});
}

static void BM_Insert_CompositeKey(benchmark::State& state) {
  bench_insert<CheesemapAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"cheesemap", "c", "CompositeKey", "Insert"});
}
static void BM_LookupHit_CompositeKey(benchmark::State& state) {
  bench_lookup_hit<CheesemapAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"cheesemap", "c", "CompositeKey", "LookupHit"});
}
static void BM_LookupMiss_CompositeKey(benchmark::State& state) {
  bench_lookup_miss<CheesemapAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"cheesemap", "c", "CompositeKey", "LookupMiss"});
}
static void BM_Erase_CompositeKey(benchmark::State& state) {
  bench_erase<CheesemapAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(), {"cheesemap", "c", "CompositeKey", "Erase"});
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
