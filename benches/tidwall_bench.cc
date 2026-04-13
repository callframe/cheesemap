#include <benchmark/benchmark.h>

#include "bench_common.h"

namespace {

using namespace cmbench;

static void BM_Insert_Scalar(benchmark::State& state) {
  bench_insert<TidwallAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(), {"tidwall_hashmap", "c", "Scalar", "Insert"});
}
static void BM_LookupHit_Scalar(benchmark::State& state) {
  bench_lookup_hit<TidwallAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(),
      {"tidwall_hashmap", "c", "Scalar", "LookupHit"});
}
static void BM_LookupMiss_Scalar(benchmark::State& state) {
  bench_lookup_miss<TidwallAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(),
      {"tidwall_hashmap", "c", "Scalar", "LookupMiss"});
}
static void BM_Erase_Scalar(benchmark::State& state) {
  bench_erase<TidwallAdapter<std::uint64_t, ScalarValue>>(
      state, scalar_workload(), {"tidwall_hashmap", "c", "Scalar", "Erase"});
}

static void BM_Insert_HandlePayload(benchmark::State& state) {
  bench_insert<TidwallAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"tidwall_hashmap", "c", "HandlePayload", "Insert"});
}
static void BM_LookupHit_HandlePayload(benchmark::State& state) {
  bench_lookup_hit<TidwallAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"tidwall_hashmap", "c", "HandlePayload", "LookupHit"});
}
static void BM_LookupMiss_HandlePayload(benchmark::State& state) {
  bench_lookup_miss<TidwallAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"tidwall_hashmap", "c", "HandlePayload", "LookupMiss"});
}
static void BM_Erase_HandlePayload(benchmark::State& state) {
  bench_erase<TidwallAdapter<EntityId, ComponentPayload>>(
      state, entity_workload(),
      {"tidwall_hashmap", "c", "HandlePayload", "Erase"});
}

static void BM_Insert_CompositeKey(benchmark::State& state) {
  bench_insert<TidwallAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"tidwall_hashmap", "c", "CompositeKey", "Insert"});
}
static void BM_LookupHit_CompositeKey(benchmark::State& state) {
  bench_lookup_hit<TidwallAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"tidwall_hashmap", "c", "CompositeKey", "LookupHit"});
}
static void BM_LookupMiss_CompositeKey(benchmark::State& state) {
  bench_lookup_miss<TidwallAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"tidwall_hashmap", "c", "CompositeKey", "LookupMiss"});
}
static void BM_Erase_CompositeKey(benchmark::State& state) {
  bench_erase<TidwallAdapter<ComponentKey, ComponentMeta>>(
      state, component_workload(),
      {"tidwall_hashmap", "c", "CompositeKey", "Erase"});
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
