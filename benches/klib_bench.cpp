#include <benchmark/benchmark.h>

#include "bench_common.hpp"

extern "C" {
#include "klib/khash.h"
}

namespace {

using namespace cmbench;

// Define khash instances for each key-value type
static inline std::uint64_t kh_hash_u64(std::uint64_t key) {
  return hash_xxh_avalanche(key);
}

static inline int kh_equal_u64(std::uint64_t a, std::uint64_t b) {
  return a == b;
}

KHASH_INIT(u64_scalar, std::uint64_t, ScalarValue, 1, kh_hash_u64, kh_equal_u64)

static inline std::uint64_t kh_hash_entity(EntityId key) {
  return hash_xxh_avalanche(key);
}

static inline int kh_equal_entity(EntityId a, EntityId b) {
  return byte_equal(a, b);
}

KHASH_INIT(entity_payload, EntityId, ComponentPayload, 1, kh_hash_entity,
           kh_equal_entity)

static inline std::uint64_t kh_hash_component(ComponentKey key) {
  return hash_xxh_avalanche(key);
}

static inline int kh_equal_component(ComponentKey a, ComponentKey b) {
  return byte_equal(a, b);
}

KHASH_INIT(component_meta, ComponentKey, ComponentMeta, 1, kh_hash_component,
           kh_equal_component)

// Adapter for u64 -> ScalarValue
struct KlibScalarAdapter {
  khash_t(u64_scalar) * map = nullptr;

  KlibScalarAdapter() { map = kh_init(u64_scalar); }

  ~KlibScalarAdapter() {
    if (map != nullptr) kh_destroy(u64_scalar, map);
  }

  KlibScalarAdapter(const KlibScalarAdapter&) = delete;
  KlibScalarAdapter& operator=(const KlibScalarAdapter&) = delete;

  void reserve(std::size_t count) { benchmark::DoNotOptimize(count); }

  void insert(const std::uint64_t& key, const ScalarValue& value) {
    int ret;
    khiter_t k = kh_put(u64_scalar, map, key, &ret);
    if (ret < 0) panic_impl(__FILE__, __LINE__, "insert failed");
    kh_value(map, k) = value;
  }

  ScalarValue* lookup_hit(const std::uint64_t& key) {
    khiter_t k = kh_get(u64_scalar, map, key);
    if (k == kh_end(map)) panic_impl(__FILE__, __LINE__, "lookup hit failed");
    return &kh_value(map, k);
  }

  void lookup_miss(const std::uint64_t& key) {
    khiter_t k = kh_get(u64_scalar, map, key);
    if (k != kh_end(map)) panic_impl(__FILE__, __LINE__, "lookup miss failed");
    benchmark::DoNotOptimize(k);
  }

  void erase(const std::uint64_t& key) {
    khiter_t k = kh_get(u64_scalar, map, key);
    if (k == kh_end(map)) panic_impl(__FILE__, __LINE__, "erase failed");
    kh_del(u64_scalar, map, k);
  }
};

// Adapter for EntityId -> ComponentPayload
struct KlibEntityAdapter {
  khash_t(entity_payload) * map = nullptr;

  KlibEntityAdapter() { map = kh_init(entity_payload); }

  ~KlibEntityAdapter() {
    if (map != nullptr) kh_destroy(entity_payload, map);
  }

  KlibEntityAdapter(const KlibEntityAdapter&) = delete;
  KlibEntityAdapter& operator=(const KlibEntityAdapter&) = delete;

  void reserve(std::size_t count) { benchmark::DoNotOptimize(count); }

  void insert(const EntityId& key, const ComponentPayload& value) {
    int ret;
    khiter_t k = kh_put(entity_payload, map, key, &ret);
    if (ret < 0) panic_impl(__FILE__, __LINE__, "insert failed");
    kh_value(map, k) = value;
  }

  ComponentPayload* lookup_hit(const EntityId& key) {
    khiter_t k = kh_get(entity_payload, map, key);
    if (k == kh_end(map)) panic_impl(__FILE__, __LINE__, "lookup hit failed");
    return &kh_value(map, k);
  }

  void lookup_miss(const EntityId& key) {
    khiter_t k = kh_get(entity_payload, map, key);
    if (k != kh_end(map)) panic_impl(__FILE__, __LINE__, "lookup miss failed");
    benchmark::DoNotOptimize(k);
  }

  void erase(const EntityId& key) {
    khiter_t k = kh_get(entity_payload, map, key);
    if (k == kh_end(map)) panic_impl(__FILE__, __LINE__, "erase failed");
    kh_del(entity_payload, map, k);
  }
};

// Adapter for ComponentKey -> ComponentMeta
struct KlibComponentAdapter {
  khash_t(component_meta) * map = nullptr;

  KlibComponentAdapter() { map = kh_init(component_meta); }

  ~KlibComponentAdapter() {
    if (map != nullptr) kh_destroy(component_meta, map);
  }

  KlibComponentAdapter(const KlibComponentAdapter&) = delete;
  KlibComponentAdapter& operator=(const KlibComponentAdapter&) = delete;

  void reserve(std::size_t count) { benchmark::DoNotOptimize(count); }

  void insert(const ComponentKey& key, const ComponentMeta& value) {
    int ret;
    khiter_t k = kh_put(component_meta, map, key, &ret);
    if (ret < 0) panic_impl(__FILE__, __LINE__, "insert failed");
    kh_value(map, k) = value;
  }

  ComponentMeta* lookup_hit(const ComponentKey& key) {
    khiter_t k = kh_get(component_meta, map, key);
    if (k == kh_end(map)) panic_impl(__FILE__, __LINE__, "lookup hit failed");
    return &kh_value(map, k);
  }

  void lookup_miss(const ComponentKey& key) {
    khiter_t k = kh_get(component_meta, map, key);
    if (k != kh_end(map)) panic_impl(__FILE__, __LINE__, "lookup miss failed");
    benchmark::DoNotOptimize(k);
  }

  void erase(const ComponentKey& key) {
    khiter_t k = kh_get(component_meta, map, key);
    if (k == kh_end(map)) panic_impl(__FILE__, __LINE__, "erase failed");
    kh_del(component_meta, map, k);
  }
};

static void BM_Insert_Scalar(benchmark::State& state) {
  bench_insert<KlibScalarAdapter, std::uint64_t, ScalarValue>(
      state, scalar_workload(), {"khash", "c", "Scalar", "Insert"});
}
static void BM_LookupHit_Scalar(benchmark::State& state) {
  bench_lookup_hit<KlibScalarAdapter, std::uint64_t, ScalarValue>(
      state, scalar_workload(), {"khash", "c", "Scalar", "LookupHit"});
}
static void BM_LookupMiss_Scalar(benchmark::State& state) {
  bench_lookup_miss<KlibScalarAdapter, std::uint64_t, ScalarValue>(
      state, scalar_workload(), {"khash", "c", "Scalar", "LookupMiss"});
}
static void BM_Erase_Scalar(benchmark::State& state) {
  bench_erase<KlibScalarAdapter, std::uint64_t, ScalarValue>(
      state, scalar_workload(), {"khash", "c", "Scalar", "Erase"});
}

static void BM_Insert_HandlePayload(benchmark::State& state) {
  bench_insert<KlibEntityAdapter, EntityId, ComponentPayload>(
      state, entity_workload(), {"khash", "c", "HandlePayload", "Insert"});
}
static void BM_LookupHit_HandlePayload(benchmark::State& state) {
  bench_lookup_hit<KlibEntityAdapter, EntityId, ComponentPayload>(
      state, entity_workload(), {"khash", "c", "HandlePayload", "LookupHit"});
}
static void BM_LookupMiss_HandlePayload(benchmark::State& state) {
  bench_lookup_miss<KlibEntityAdapter, EntityId, ComponentPayload>(
      state, entity_workload(),
      {"khash", "c", "HandlePayload", "LookupMiss"});
}
static void BM_Erase_HandlePayload(benchmark::State& state) {
  bench_erase<KlibEntityAdapter, EntityId, ComponentPayload>(
      state, entity_workload(), {"khash", "c", "HandlePayload", "Erase"});
}

static void BM_Insert_CompositeKey(benchmark::State& state) {
  bench_insert<KlibComponentAdapter, ComponentKey, ComponentMeta>(
      state, component_workload(), {"khash", "c", "CompositeKey", "Insert"});
}
static void BM_LookupHit_CompositeKey(benchmark::State& state) {
  bench_lookup_hit<KlibComponentAdapter, ComponentKey, ComponentMeta>(
      state, component_workload(),
      {"khash", "c", "CompositeKey", "LookupHit"});
}
static void BM_LookupMiss_CompositeKey(benchmark::State& state) {
  bench_lookup_miss<KlibComponentAdapter, ComponentKey, ComponentMeta>(
      state, component_workload(),
      {"khash", "c", "CompositeKey", "LookupMiss"});
}
static void BM_Erase_CompositeKey(benchmark::State& state) {
  bench_erase<KlibComponentAdapter, ComponentKey, ComponentMeta>(
      state, component_workload(), {"khash", "c", "CompositeKey", "Erase"});
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
