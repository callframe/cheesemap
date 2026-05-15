Cheesemap
=========

Cheesemap is a fast C++17 hash map and hash set. It uses a
Swiss-table-style open-addressing layout with control bytes, SIMD group probing
on SSE2 targets, and a scalar fallback.

Cheesemap is intentionally one public header-style source file:

    cheesemap.cc

It will stay that way.

Use it when you want a direct hash table and want to provide hashing, equality,
allocation, and deallocation functions yourself.

Cheesemap is designed for C-style C++ use. It does not try to integrate with
C++ object lifetime rules. There is no rule of three, rule of five, copy
constructor, move constructor, or automatic construction/destruction of stored
objects. Use types that tolerate that model. If a type needs constructors,
destructors, ownership hooks, or exception-safe movement, that is the caller's
problem.


Status
------

Cheesemap is not production-proven yet. That is the caveat.

It is not a work-in-progress placeholder. In the included integer-key
benchmarks it is close to Abseil `flat_hash_map`, the reference implementation
in this comparison, and it is ahead of the other implementations shown in most
of the chart.

The benchmark suite compares Cheesemap with `std::unordered_map`, Abseil
`flat_hash_map`, khash, and an older Cheesemap implementation.

Throughput from the current benchmark set:

![Cheesemap benchmark throughput](benches/throughput.png)


Installation
------------

Cheesemap does not require a build system. Copy `cheesemap.cc` into your
project and include it.

The repository supports Bazel and CMake as well.

With Bazel:

    bazel build //:cheesemap

With CMake:

    cmake -S . -B build
    cmake --build build

Cheesemap requires C++17 or later. MSVC is not supported at this time.


Benchmarks
----------

The benchmark targets are under `benches/`.

With Bazel:

    bazel build //benches:cheesemap_bench
    bazel build //benches:abseil_bench
    bazel build //benches:std_unordered_map_bench
    bazel build //benches:khash_bench

The benchmarks measure insert, replace, hit lookup, miss lookup, and remove
throughput over table sizes from 1024 to 1048576 entries.

The chart above was produced through Bazel on WSL2 under Windows 11, using
native instructions and `-O2`. The machine used a 13th Gen Intel Core
i7-13850HX with normal CPU scaling enabled. It was not configured as a dedicated
benchmark machine; the numbers reflect whatever Windows scheduling and power
management did at the time. The runtime libc was GNU libc 6.


Use
---

Include `cheesemap.cc` and instantiate `Cheesemap` with key type, value type,
hash function, equality function, allocator, and deallocator:

    using Map = Cheesemap<Key, Value, Hash, Equal, Alloc, Dealloc>;

The basic operations are:

    cheesemap_new
    cheesemap_new_with
    cheesemap_drop
    cheesemap_reserve
    cheesemap_insert
    cheesemap_lookup
    cheesemap_remove

`Cheeseset` provides the same storage strategy for set membership.
