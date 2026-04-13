#ifndef CHEESEMAP_BENCH_COMMON_HPP
#define CHEESEMAP_BENCH_COMMON_HPP

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "cheesemap.h"
#include "tidwall/hashmap.h"
#include "absl/container/flat_hash_map.h"


namespace cmbench {

inline constexpr std::int64_t kEntryCount = 1'000'000;
inline constexpr std::string_view kProtocolVersion = "1";
inline constexpr std::string_view kDatasetVersion = "1";
inline constexpr std::string_view kHashQuality = "good";
inline constexpr std::string_view kHashName = "xxh_avalanche";

[[noreturn]] inline void panic_impl(const char* file, cm_u32 line,
                                    const char* fmt, ...) {
  std::fprintf(stderr, "panic at %s:%u: ", file, line);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::abort();
}

inline cm_u8* default_alloc(cm_usize size, cm_usize align, cm_u8* user) {
  (void)user;
  return static_cast<cm_u8*>(std::aligned_alloc(align, size));
}

inline void default_dealloc(cm_u8* ptr, cm_u8* user) {
  (void)user;
  std::free(ptr);
}

inline std::uint64_t rotl64(std::uint64_t x, unsigned bits) {
  return (x << bits) | (x >> (64 - bits));
}

inline std::uint64_t xxh64_avalanche(std::uint64_t x) {
  x ^= x >> 33;
  x *= 0xc2b2ae3d27d4eb4fULL;
  x ^= x >> 29;
  x *= 0x165667b19e3779f9ULL;
  x ^= x >> 32;
  return x;
}

template <typename T>
inline std::array<std::uint64_t, (sizeof(T) + 7) / 8> as_words(const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  std::array<std::uint64_t, (sizeof(T) + 7) / 8> words{};
  std::memcpy(words.data(), &value, sizeof(T));
  return words;
}

template <typename T>
inline std::uint64_t hash_xxh_avalanche(const T& value) {
  auto words = as_words(value);
  std::uint64_t acc = 0x165667b19e3779f9ULL ^ sizeof(T);
  for (std::uint64_t word : words) {
    acc += word * 0x9e3779b185ebca87ULL;
    acc = rotl64(acc, 31);
    acc *= 0xc2b2ae3d27d4eb4fULL;
  }
  return xxh64_avalanche(acc);
}

template <typename T>
inline bool byte_equal(const T& lhs, const T& rhs) {
  static_assert(std::is_trivially_copyable_v<T>);
  return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

struct ScalarValue {
  std::uint64_t value;
};

struct EntityId {
  std::uint32_t index;
  std::uint32_t generation;
};

struct ComponentPayload {
  float position[3];
  float velocity[3];
  std::uint32_t archetype;
  std::uint32_t chunk;
};

struct ComponentKey {
  std::uint64_t archetype_mask;
  std::uint32_t component_id;
  std::uint32_t chunk;
  std::uint32_t row;
  std::uint32_t generation;
};

struct ComponentMeta {
  std::uint64_t entity_mask;
  std::uint32_t dense_index;
  std::uint32_t sparse_index;
  std::uint64_t version;
};

template <typename Key, typename Value>
struct Workload {
  std::vector<Key> keys;
  std::vector<Key> miss_keys;
  std::vector<Value> values;
};

template <typename Key>
inline void shuffle_keys(std::vector<Key>& keys, std::mt19937_64& rng) {
  std::shuffle(keys.begin(), keys.end(), rng);
}

inline Workload<std::uint64_t, ScalarValue> make_scalar_workload(
    std::size_t count) {
  Workload<std::uint64_t, ScalarValue> workload{
      .keys = std::vector<std::uint64_t>(count),
      .miss_keys = std::vector<std::uint64_t>(count),
      .values = std::vector<ScalarValue>(count),
  };

  std::mt19937_64 rng(0xc0ffee5eedULL);
  for (std::size_t i = 0; i < count; ++i) {
    workload.keys[i] = xxh64_avalanche(i + 1);
    workload.miss_keys[i] = xxh64_avalanche(i + 1 + count);
    workload.values[i] =
        ScalarValue{xxh64_avalanche(i ^ 0x123456789abcdef0ULL)};
  }
  shuffle_keys(workload.keys, rng);
  shuffle_keys(workload.miss_keys, rng);
  return workload;
}

inline Workload<EntityId, ComponentPayload> make_entity_workload(
    std::size_t count) {
  Workload<EntityId, ComponentPayload> workload{
      .keys = std::vector<EntityId>(count),
      .miss_keys = std::vector<EntityId>(count),
      .values = std::vector<ComponentPayload>(count),
  };

  std::mt19937_64 rng(0xEC500123ULL);
  for (std::size_t i = 0; i < count; ++i) {
    workload.keys[i] = EntityId{static_cast<std::uint32_t>(i),
                                static_cast<std::uint32_t>(i >> 12)};
    workload.miss_keys[i] = EntityId{static_cast<std::uint32_t>(i),
                                     static_cast<std::uint32_t>((i >> 12) + 1)};

    float base = static_cast<float>(i & 0xffff);
    workload.values[i] = ComponentPayload{
        .position = {base + 0.1f, base + 0.2f, base + 0.3f},
        .velocity = {base + 1.1f, base + 1.2f, base + 1.3f},
        .archetype = static_cast<std::uint32_t>(xxh64_avalanche(i) & 0xffff),
        .chunk = static_cast<std::uint32_t>(i / 64),
    };
  }
  shuffle_keys(workload.keys, rng);
  shuffle_keys(workload.miss_keys, rng);
  return workload;
}

inline Workload<ComponentKey, ComponentMeta> make_component_workload(
    std::size_t count) {
  Workload<ComponentKey, ComponentMeta> workload{
      .keys = std::vector<ComponentKey>(count),
      .miss_keys = std::vector<ComponentKey>(count),
      .values = std::vector<ComponentMeta>(count),
  };

  std::mt19937_64 rng(0xA11CECC5ULL);
  for (std::size_t i = 0; i < count; ++i) {
    std::uint64_t archetype = xxh64_avalanche(i * 0x9e3779b97f4a7c15ULL);
    workload.keys[i] = ComponentKey{
        .archetype_mask = archetype,
        .component_id = static_cast<std::uint32_t>((i * 17) & 0xffff),
        .chunk = static_cast<std::uint32_t>(i / 64),
        .row = static_cast<std::uint32_t>(i % 64),
        .generation = static_cast<std::uint32_t>(i >> 10),
    };
    workload.miss_keys[i] = ComponentKey{
        .archetype_mask = archetype ^ 0xfeedfacecafebeefULL,
        .component_id = static_cast<std::uint32_t>((i * 17) & 0xffff),
        .chunk = static_cast<std::uint32_t>(i / 64),
        .row = static_cast<std::uint32_t>(i % 64),
        .generation = static_cast<std::uint32_t>(i >> 10),
    };
    workload.values[i] = ComponentMeta{
        .entity_mask = xxh64_avalanche(i + 7),
        .dense_index = static_cast<std::uint32_t>(i),
        .sparse_index =
            static_cast<std::uint32_t>(xxh64_avalanche(i) & 0xffffffffU),
        .version = xxh64_avalanche(i ^ 0xabcdef01ULL),
    };
  }
  shuffle_keys(workload.keys, rng);
  shuffle_keys(workload.miss_keys, rng);
  return workload;
}

inline const Workload<std::uint64_t, ScalarValue>& scalar_workload() {
  static const auto workload = make_scalar_workload(kEntryCount);
  return workload;
}

inline const Workload<EntityId, ComponentPayload>& entity_workload() {
  static const auto workload = make_entity_workload(kEntryCount);
  return workload;
}

inline const Workload<ComponentKey, ComponentMeta>& component_workload() {
  static const auto workload = make_component_workload(kEntryCount);
  return workload;
}

template <typename T>
inline cm_hash_t cheesemap_hash_adapter(const cm_u8* key, cm_u8* user) {
  (void)user;
  return hash_xxh_avalanche(*reinterpret_cast<const T*>(key));
}

template <typename T>
inline bool cheesemap_equal_adapter(const cm_u8* lhs, const cm_u8* rhs,
                                    cm_u8* user) {
  (void)user;
  return byte_equal(*reinterpret_cast<const T*>(lhs),
                    *reinterpret_cast<const T*>(rhs));
}

struct Meta {
  std::string_view implementation;
  std::string_view language;
  std::string_view dataset;
  std::string_view workload_category;
};

inline void set_common_metadata(benchmark::State& state, const Meta& meta) {
  std::string label;
  label.reserve(128);
  label.append("implementation=");
  label.append(meta.implementation);
  label.append(",language=");
  label.append(meta.language);
  label.append(",dataset=");
  label.append(meta.dataset);
  label.append(",workload=");
  label.append(meta.workload_category);
  label.append(",hash_quality=");
  label.append(kHashQuality);
  label.append(",hash=");
  label.append(kHashName);
  label.append(",protocol=");
  label.append(kProtocolVersion);
  label.append(",dataset_version=");
  label.append(kDatasetVersion);
  state.SetLabel(label);
  state.counters["entry_count"] =
      benchmark::Counter(static_cast<double>(kEntryCount));
}

template <typename Key, typename Value>
struct CheesemapAdapter {
  cheesemap map;

  CheesemapAdapter() {
    cm_init(&map, sizeof(Key), alignof(Key), sizeof(Value), alignof(Value),
            nullptr, cheesemap_hash_adapter<Key>, cheesemap_equal_adapter<Key>,
            default_alloc, default_dealloc);
  }

  ~CheesemapAdapter() { cm_drop(&map); }

  CheesemapAdapter(const CheesemapAdapter&) = delete;
  CheesemapAdapter& operator=(const CheesemapAdapter&) = delete;

  void reserve(std::size_t count) {
    if (!cm_reserve(&map, count))
      panic_impl(__FILE__, __LINE__, "reserve failed");
  }

  void insert(const Key& key, const Value& value) {
    if (!cm_insert(&map, reinterpret_cast<const cm_u8*>(&key),
                   reinterpret_cast<const cm_u8*>(&value))) {
      panic_impl(__FILE__, __LINE__, "insert failed");
    }
  }

  Value* lookup_hit(const Key& key) {
    cm_u8* value_ptr = nullptr;
    if (!cm_lookup(&map, reinterpret_cast<const cm_u8*>(&key), &value_ptr))
      panic_impl(__FILE__, __LINE__, "lookup hit failed");
    return reinterpret_cast<Value*>(value_ptr);
  }

  void lookup_miss(const Key& key) {
    cm_u8* value_ptr = nullptr;
    if (cm_lookup(&map, reinterpret_cast<const cm_u8*>(&key), &value_ptr))
      panic_impl(__FILE__, __LINE__, "lookup miss failed");
    benchmark::DoNotOptimize(value_ptr);
  }

  void erase(const Key& key) {
    if (!cm_remove(&map, reinterpret_cast<const cm_u8*>(&key), nullptr))
      panic_impl(__FILE__, __LINE__, "erase failed");
  }
};

template <typename Key>
struct UnorderedHashAdapter {
  std::size_t operator()(const Key& key) const noexcept {
    return static_cast<std::size_t>(hash_xxh_avalanche(key));
  }
};

template <typename Key>
struct UnorderedEqualAdapter {
  bool operator()(const Key& lhs, const Key& rhs) const noexcept {
    return byte_equal(lhs, rhs);
  }
};

template <typename Key, typename Value>
struct UnorderedMapAdapter {
  using Map = std::unordered_map<Key, Value, UnorderedHashAdapter<Key>,
                                 UnorderedEqualAdapter<Key>>;

  Map map;

  void reserve(std::size_t count) { map.reserve(count); }

  void insert(const Key& key, const Value& value) {
    map.insert_or_assign(key, value);
  }

  Value* lookup_hit(const Key& key) {
    auto it = map.find(key);
    if (it == map.end()) panic_impl(__FILE__, __LINE__, "lookup hit failed");
    return &it->second;
  }

  void lookup_miss(const Key& key) {
    auto it = map.find(key);
    if (it != map.end()) panic_impl(__FILE__, __LINE__, "lookup miss failed");
    benchmark::DoNotOptimize(it);
  }

  void erase(const Key& key) {
    if (map.erase(key) != 1) panic_impl(__FILE__, __LINE__, "erase failed");
  }
};

template <typename Key, typename Value>
struct TidwallEntry {
  Key key;
  Value value;
};

template <typename Key, typename Value>
struct TidwallAdapter {
  using Entry = TidwallEntry<Key, Value>;

  hashmap* map = nullptr;

  static std::uint64_t hash_entry(const void* item, std::uint64_t seed0,
                                  std::uint64_t seed1) {
    (void)seed0;
    (void)seed1;
    const auto* entry = static_cast<const Entry*>(item);
    return hash_xxh_avalanche(entry->key);
  }

  static int compare_entry(const void* lhs, const void* rhs, void* udata) {
    (void)udata;
    const auto* left = static_cast<const Entry*>(lhs);
    const auto* right = static_cast<const Entry*>(rhs);
    return byte_equal(left->key, right->key) ? 0 : 1;
  }

  TidwallAdapter() {
    map = hashmap_new(sizeof(Entry), static_cast<std::size_t>(kEntryCount), 0,
                      0, hash_entry, compare_entry, nullptr, nullptr);
    if (map == nullptr) panic_impl(__FILE__, __LINE__, "hashmap_new failed");
  }

  ~TidwallAdapter() {
    if (map != nullptr) hashmap_free(map);
  }

  TidwallAdapter(const TidwallAdapter&) = delete;
  TidwallAdapter& operator=(const TidwallAdapter&) = delete;

  void reserve(std::size_t count) { benchmark::DoNotOptimize(count); }

  void insert(const Key& key, const Value& value) {
    const Entry entry{key, value};
    if (hashmap_set(map, &entry) == nullptr && hashmap_oom(map))
      panic_impl(__FILE__, __LINE__, "insert failed");
  }

  Value* lookup_hit(const Key& key) {
    const Entry query{key, Value{}};
    auto* found = static_cast<const Entry*>(hashmap_get(map, &query));
    if (found == nullptr) panic_impl(__FILE__, __LINE__, "lookup hit failed");
    return const_cast<Value*>(&found->value);
  }

  void lookup_miss(const Key& key) {
    const Entry query{key, Value{}};
    auto* found = static_cast<const Entry*>(hashmap_get(map, &query));
    if (found != nullptr) panic_impl(__FILE__, __LINE__, "lookup miss failed");
    benchmark::DoNotOptimize(found);
  }

  void erase(const Key& key) {
    const Entry query{key, Value{}};
    if (hashmap_delete(map, &query) == nullptr)
      panic_impl(__FILE__, __LINE__, "erase failed");
  }
};

template <typename Key, typename Value>
struct AbseilAdapter {
  using Map = absl::flat_hash_map<Key, Value, UnorderedHashAdapter<Key>,
                                  UnorderedEqualAdapter<Key>>;

  Map map;

  void reserve(std::size_t count) { map.reserve(count); }

  void insert(const Key& key, const Value& value) {
    map.insert_or_assign(key, value);
  }

  Value* lookup_hit(const Key& key) {
    auto it = map.find(key);
    if (it == map.end()) panic_impl(__FILE__, __LINE__, "lookup hit failed");
    return &it->second;
  }

  void lookup_miss(const Key& key) {
    auto it = map.find(key);
    if (it != map.end()) panic_impl(__FILE__, __LINE__, "lookup miss failed");
    benchmark::DoNotOptimize(it);
  }

  void erase(const Key& key) {
    if (map.erase(key) != 1) panic_impl(__FILE__, __LINE__, "erase failed");
  }
};

template <typename Adapter, typename Key, typename Value>
inline void fill_container(Adapter& adapter,
                           const Workload<Key, Value>& workload) {
  adapter.reserve(workload.keys.size());
  for (std::size_t i = 0; i < workload.keys.size(); ++i)
    adapter.insert(workload.keys[i], workload.values[i]);
}

template <typename Adapter, typename Key, typename Value>
inline void bench_insert(benchmark::State& state,
                         const Workload<Key, Value>& workload,
                         const Meta& meta) {
  set_common_metadata(state, meta);
  for (auto _ : state) {
    Adapter adapter;
    fill_container(adapter, workload);
    benchmark::ClobberMemory();
  }
}

template <typename Adapter, typename Key, typename Value>
inline void bench_lookup_hit(benchmark::State& state,
                             const Workload<Key, Value>& workload,
                             const Meta& meta) {
  set_common_metadata(state, meta);
  for (auto _ : state) {
    state.PauseTiming();
    Adapter adapter;
    fill_container(adapter, workload);
    state.ResumeTiming();

    for (const Key& key : workload.keys) {
      Value* value_ptr = adapter.lookup_hit(key);
      benchmark::DoNotOptimize(value_ptr);
    }
    benchmark::ClobberMemory();
  }
}

template <typename Adapter, typename Key, typename Value>
inline void bench_lookup_miss(benchmark::State& state,
                              const Workload<Key, Value>& workload,
                              const Meta& meta) {
  set_common_metadata(state, meta);
  for (auto _ : state) {
    state.PauseTiming();
    Adapter adapter;
    fill_container(adapter, workload);
    state.ResumeTiming();

    for (const Key& key : workload.miss_keys) adapter.lookup_miss(key);
    benchmark::ClobberMemory();
  }
}

template <typename Adapter, typename Key, typename Value>
inline void bench_erase(benchmark::State& state,
                        const Workload<Key, Value>& workload,
                        const Meta& meta) {
  set_common_metadata(state, meta);
  for (auto _ : state) {
    state.PauseTiming();
    Adapter adapter;
    fill_container(adapter, workload);
    state.ResumeTiming();

    for (const Key& key : workload.keys) adapter.erase(key);
    benchmark::ClobberMemory();
  }
}

}  // namespace cmbench

#endif
