#pragma once

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using BenchKey = std::uint64_t;
using BenchValue = std::uint64_t;

inline std::uint64_t BenchMix(std::uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

inline std::uint64_t BenchHashKey(BenchKey key)
{
    return BenchMix(key);
}

struct BenchDataSet {
    std::vector<BenchKey> keys;
    std::vector<BenchKey> misses;
    std::vector<BenchValue> values;
    std::vector<BenchValue> replacements;
};

inline const BenchDataSet& BenchData(std::size_t size)
{
    static constexpr std::size_t kMaxSize = 1u << 20;
    if (size > kMaxSize) {
        throw std::invalid_argument("benchmark size exceeds generated data set");
    }

    static const BenchDataSet data = [] {
        BenchDataSet result;
        result.keys.reserve(kMaxSize);
        result.misses.reserve(kMaxSize);
        result.values.reserve(kMaxSize);
        result.replacements.reserve(kMaxSize);

        for (std::size_t i = 0; i < kMaxSize; ++i) {
            result.keys.push_back(BenchMix(i));
            result.misses.push_back(BenchMix(i + kMaxSize));
            result.values.push_back(BenchMix(i ^ 0xabcdefu));
            result.replacements.push_back(BenchMix(i ^ 0x123456u));
        }

        return result;
    }();

    return data;
}

template <typename Adapter>
void BenchFill(Adapter& map, const BenchDataSet& data, std::size_t size)
{
    map.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        if (!map.insert(data.keys[i], data.values[i])) {
            std::abort();
        }
    }
}

inline void BenchSetThroughput(benchmark::State& state, std::size_t items_per_iteration)
{
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(items_per_iteration));
}

template <typename Adapter>
void BM_Insert(benchmark::State& state)
{
    const std::size_t size = static_cast<std::size_t>(state.range(0));
    const BenchDataSet& data = BenchData(size);

    for (auto _ : state) {
        Adapter map;
        map.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            benchmark::DoNotOptimize(map.insert(data.keys[i], data.values[i]));
        }
        benchmark::ClobberMemory();
    }

    BenchSetThroughput(state, size);
}

template <typename Adapter>
void BM_Replace(benchmark::State& state)
{
    const std::size_t size = static_cast<std::size_t>(state.range(0));
    const BenchDataSet& data = BenchData(size);

    Adapter map;
    BenchFill(map, data, size);

    for (auto _ : state) {
        for (std::size_t i = 0; i < size; ++i) {
            benchmark::DoNotOptimize(map.replace(data.keys[i], data.replacements[i]));
        }
        benchmark::ClobberMemory();
    }

    BenchSetThroughput(state, size);
}

template <typename Adapter>
void BM_LookupHit(benchmark::State& state)
{
    const std::size_t size = static_cast<std::size_t>(state.range(0));
    const BenchDataSet& data = BenchData(size);

    Adapter map;
    BenchFill(map, data, size);

    for (auto _ : state) {
        BenchValue value = 0;
        for (std::size_t i = 0; i < size; ++i) {
            benchmark::DoNotOptimize(map.lookup(data.keys[i], value));
            benchmark::DoNotOptimize(value);
        }
    }

    BenchSetThroughput(state, size);
}

template <typename Adapter>
void BM_LookupMiss(benchmark::State& state)
{
    const std::size_t size = static_cast<std::size_t>(state.range(0));
    const BenchDataSet& data = BenchData(size);

    Adapter map;
    BenchFill(map, data, size);

    for (auto _ : state) {
        BenchValue value = 0;
        for (std::size_t i = 0; i < size; ++i) {
            benchmark::DoNotOptimize(map.lookup(data.misses[i], value));
            benchmark::DoNotOptimize(value);
        }
    }

    BenchSetThroughput(state, size);
}

template <typename Adapter>
void BM_Remove(benchmark::State& state)
{
    const std::size_t size = static_cast<std::size_t>(state.range(0));
    const BenchDataSet& data = BenchData(size);

    for (auto _ : state) {
        state.PauseTiming();
        Adapter map;
        BenchFill(map, data, size);
        state.ResumeTiming();

        for (std::size_t i = 0; i < size; ++i) {
            benchmark::DoNotOptimize(map.remove(data.keys[i]));
        }
        benchmark::ClobberMemory();
    }

    BenchSetThroughput(state, size);
}

template <typename Adapter>
void RegisterBenchmarks(std::string_view name)
{
    const std::string prefix(name);
    benchmark::RegisterBenchmark((prefix + "/insert").c_str(), &BM_Insert<Adapter>)->RangeMultiplier(4)->Range(1 << 10, 1 << 20)->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark((prefix + "/replace").c_str(), &BM_Replace<Adapter>)->RangeMultiplier(4)->Range(1 << 10, 1 << 20)->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark((prefix + "/lookup_hit").c_str(), &BM_LookupHit<Adapter>)->RangeMultiplier(4)->Range(1 << 10, 1 << 20)->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark((prefix + "/lookup_miss").c_str(), &BM_LookupMiss<Adapter>)->RangeMultiplier(4)->Range(1 << 10, 1 << 20)->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark((prefix + "/remove").c_str(), &BM_Remove<Adapter>)->RangeMultiplier(4)->Range(1 << 10, 1 << 20)->Unit(benchmark::kMillisecond);
}
