#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(_MSVC_LANG)
#define CM_LANG _MSVC_LANG

// ARM64EC compiles to ARM64 but still defines _M_X64, so it needs excluding.
#if (defined(_M_X64) && !defined(_M_ARM64EC)) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define CM_SSE2 1
#else
#define CM_SSE2 0
#endif

// GCC, Clang
#elif defined(__cplusplus)
#define CM_LANG __cplusplus

#if defined(__SSE2__)
#define CM_SSE2 1
#else
#define CM_SSE2 0
#endif

#else
#error "Cheesemap requires C++"
#endif

#if CM_LANG < 202002L
#error "Cheesemap requires C++20 or later"
#endif

#if CM_SSE2
#include <emmintrin.h>
#endif

namespace cheesemap
{

/**
 *
 * Hash and compare operations
 */

using Hash = std::uint64_t;

/**
 *
 * Everything in `impl` is internal machinery and may change at any time. The
 * API is the traits, the containers and their iterators, and the `map_` and
 * `set_` operations, which live directly in `cheesemap`.
 */

namespace impl
{

/**
 *
 * Always_False is false for every type, but only once instantiated. A primary
 * trait template can static_assert on it to fail with a readable message when
 * used, while still being a well-formed template until then. Unlike
 * `sizeof(T) == 0` this is also valid for T = void.
 */

template <typename>
struct Always_False : std::false_type
{};

}  // namespace impl

/**
 *
 * Mapable is the key trait. A type K may be used as a key only if the caller
 * specializes Mapable<K> with two static members:
 *
 *   static Hash hash(K key);
 *   static bool compare(K key0, K key1);
 *
 * The primary template is never a valid trait: using an unspecialized key type
 * is a hard compile error. We make no assumptions about how an arbitrary type
 * should hash or compare.
 */

template <typename K>
struct Mapable
{
  static_assert(impl::Always_False<K>::value,
                "cheesemap::Mapable<K> is not specialized for this key type. Provide a "
                "specialization with `static Hash hash(K)` and `static bool compare(K, K)`.");
};

/**
 *
 * Allocatable is the allocator trait. The allocator's state type A may be used
 * only if the caller specializes Allocatable<A> with two static members:
 *
 *   static std::uint8_t* alloc(A* state, std::size_t size, std::size_t align);
 *   static void dealloc(A* state, std::uint8_t* ptr, std::size_t size, std::size_t align);
 *
 * State is passed by pointer. As with Mapable, an unspecialized state type is a
 * hard compile error.
 */

template <typename A>
struct Allocatable
{
  static_assert(impl::Always_False<A>::value,
                "cheesemap::Allocatable<A> is not specialized for this allocator state type. "
                "Provide a specialization with `static std::uint8_t* alloc(A*, std::size_t, "
                "std::size_t)` and "
                "`static void dealloc(A*, std::uint8_t*, std::size_t, std::size_t)`.");
};

namespace impl
{

/**
 *
 * Mapable_Check validates that a Mapable<K> specialization exposes hash and
 * compare with the exact expected signatures. A specialization whose hash or
 * compare has the wrong signature fails these asserts with a message naming the
 * signature the trait requires.
 */

template <typename K>
struct Mapable_Check
{
  using Hash_Fn = Hash (*)(K const&);
  using In_Hash_Fn = decltype(&Mapable<K>::hash);
  static_assert(std::is_same<Hash_Fn, In_Hash_Fn>::value,
                "cheesemap::Mapable<K>::hash must be `static Hash hash(K const&)`.");

  using Compare_Fn = bool (*)(K const&, K const&);
  using In_Compare_Fn = decltype(&Mapable<K>::compare);
  static_assert(
      std::is_same<Compare_Fn, In_Compare_Fn>::value,
      "cheesemap::Mapable<K>::compare must be `static bool compare(K const&, K const&)`.");
};

template <typename K>
Hash hash(K const& k)
{
  (void)Mapable_Check<K>{};
  return Mapable<K>::hash(k);
}

template <typename K>
bool compare(K const& a, K const& b)
{
  (void)Mapable_Check<K>{};
  return Mapable<K>::compare(a, b);
}

/**
 *
 * Allocatable_Check validates that an Allocatable<A> specialization exposes
 * alloc and dealloc with the exact expected signatures. A specialization with
 * the wrong signature fails these asserts with a message naming the signature
 * the trait requires.
 */

template <typename A>
struct Allocatable_Check
{
  using Alloc_Fn = std::uint8_t* (*)(A*, std::size_t, std::size_t);
  using In_Alloc_Fn = decltype(&Allocatable<A>::alloc);
  static_assert(
      std::is_same<Alloc_Fn, In_Alloc_Fn>::value,
      "cheesemap::Allocatable<A>::alloc must be `static std::uint8_t* alloc(A*, std::size_t, "
      "std::size_t)`.");

  using Dealloc_Fn = void (*)(A*, std::uint8_t*, std::size_t, std::size_t);
  using In_Dealloc_Fn = decltype(&Allocatable<A>::dealloc);
  static_assert(
      std::is_same<Dealloc_Fn, In_Dealloc_Fn>::value,
      "cheesemap::Allocatable<A>::dealloc must be `static void dealloc(A*, std::uint8_t*, "
      "std::size_t, std::size_t)`.");
};

template <typename A>
std::uint8_t* alloc(A* state, std::size_t size, std::size_t align)
{
  (void)Allocatable_Check<A>{};
  return Allocatable<A>::alloc(state, size, align);
}

template <typename A>
void dealloc(A* state, std::uint8_t* ptr, std::size_t size, std::size_t align)
{
  (void)Allocatable_Check<A>{};
  Allocatable<A>::dealloc(state, ptr, size, align);
}

/**
 *
 * Control-byte encoding.
 *
 * Each bucket has one control byte. EMPTY and DELETED are special states with
 * the high bit set. FULL buckets store the 7-bit H2 hash fingerprint with the
 * high bit clear, which lets group matching test many buckets at once.
 *
 * The control array also stores a cloned prefix of Group_Size bytes after
 * the real buckets, so group loads can wrap around the end of the table.
 */

enum : std::uint8_t
{
  // cheesemap config
  Load_Denom = 8,
  Load_Num = 7,
  //
  // ctrl ops
  // -1 as i8, all bits set, top bit = 1
  Ctrl_Empty = 0xFF,    // 0b1111_1111
                        // -128 as i8, top bit = 1
  Ctrl_Deleted = 0x80,  // 0b1000_0000
                        // FULL entries have top bit = 0, lower 7 bits are H2 hash
  H2_Mask = 0x7F,       // 0b0111_1111
                        // Mask to get bottom bit
  Ctrl_End = 0x01,      // 0b0000_0001
                        // Number of fingerprint bits
  Fp_Size = 7,
  //
  // aux
  // Size of a word in bits
  Word_Width = sizeof(std::size_t) * CHAR_BIT,
};

#if CM_SSE2
using Group = __m128i;
using Bitmask = std::uint16_t;

constexpr inline std::size_t Group_Size = 16;
constexpr inline std::size_t Bitmask_Stride = 1;

#else
using Group = std::size_t;
using Bitmask = Group;

constexpr inline std::size_t Group_Size = sizeof(std::size_t);
constexpr inline std::size_t Bitmask_Stride = CHAR_BIT;
#endif

static_assert(Group_Size == 16 || Group_Size == 8 || Group_Size == 4,
              "cheesemap supports group sizes of 4, 8 or 16 bytes");

/**
 *
 * Group actions interface
 */

// TODO: check whether passing my pointer is faster
inline Group group_load(std::uint8_t const* ctrl);
inline Bitmask group_match_tag(Group group, std::uint8_t tag);
inline Bitmask group_match_empty_or_deleted(Group group);
inline Bitmask group_match_empty(Group group);
inline Bitmask group_match_full(Group group);

/**
 *
 * SSE2 implementation of the group actions
 */

#if CM_SSE2
inline Group group_load(std::uint8_t const* ctrl) { return _mm_loadu_si128((Group const*)ctrl); }

inline Bitmask group_match_tag(Group group, std::uint8_t tag)
{
  __m128i const tagvec = _mm_set1_epi8(tag);
  __m128i cmp = _mm_cmpeq_epi8(group, tagvec);
  // movemask packs the top bit of each byte into a 16-bit mask, giving one
  // candidate bit per ctrl byte in the loaded group.
  return _mm_movemask_epi8(cmp);
}

inline Bitmask group_match_empty_or_deleted(Group group)
{
  // EMPTY and DELETED both have their top bit set, so movemask directly gives
  // the "special ctrl byte" mask for the whole group.
  return _mm_movemask_epi8(group);
}

inline Bitmask group_match_empty(Group group) { return group_match_tag(group, Ctrl_Empty); }

inline Bitmask group_match_full(Group group)
{
  // FULL ctrl bytes clear the top bit, so the full-slot mask is just the
  // inverse of the special-slot mask for this 16-byte group.
  return ~group_match_empty_or_deleted(group);
}

/**
 *
 * Scalar implementation of the group actions
 */

#else
inline Group group_repeat(std::uint8_t v) { return (Group)v * (((Group)-1) / (std::uint8_t)~0); }

inline Group group_load(std::uint8_t const* ctrl)
{
  Group v;
  std::memcpy(&v, ctrl, sizeof(v));
  return v;
}

inline Bitmask group_match_empty_or_deleted(Group group)
{
  return group & group_repeat(Ctrl_Deleted);
}

inline Bitmask group_match_empty(Group group)
{
  return (group & (group << 1)) & group_repeat(Ctrl_Deleted);
}

inline Bitmask group_match_full(Group group)
{
  return group_match_empty_or_deleted(group) ^ group_repeat(Ctrl_Deleted);
}

inline Bitmask group_match_tag(Group group, std::uint8_t tag)
{
  Group cmp = group ^ group_repeat(tag);
  return (cmp - group_repeat(Ctrl_End)) & ~cmp & group_repeat(Ctrl_Deleted);
}

#endif

/**
 *
 * Initial control block for uninitialized maps.
 *
 * This normalizes behavior between an uninitialized map and a map that has
 * allocated storage but needs to grow. Inserts do not special-case a null
 * control pointer. Instead, they probe this all-EMPTY block, notice that the
 * map has no growth left, and resize before writing.
 */

constexpr std::array<std::uint8_t, Group_Size> init_ctrl_new()
{
  std::array<std::uint8_t, Group_Size> block{};
  for (auto& byte : block) byte = Ctrl_Empty;
  return block;
}

constexpr inline auto Init_Ctrl = init_ctrl_new();

/**
 *
 * Return the number of trailing zero bits in a bitmask.
 * Returns Group_Size when the mask is zero.
 */

inline std::uint32_t bitmask_trailing_zeros(Bitmask mask)
{
  return (std::uint32_t)std::countr_zero(mask) / (std::uint32_t)Bitmask_Stride;
}

inline std::uint32_t bitmask_leading_zeros(Bitmask mask)
{
  // Must return slot units, like bitmask_trailing_zeros.
  if constexpr (Bitmask_Stride == 1)
  {
    // Dense bitmask, one bit per slot
    return (std::uint32_t)std::countl_zero(mask) - (std::uint32_t)(Word_Width - Group_Size);
  }
  else
  {
    // SWAR bitmask, one byte per slot
    return (std::uint32_t)std::countl_zero(mask) / (std::uint32_t)Bitmask_Stride;
  }
}

[[maybe_unused]] inline bool is_pow2(std::size_t x) { return std::has_single_bit(x); }

template <typename T>
constexpr T max(T x, T y)
{
  return x > y ? x : y;
}

inline std::size_t next_pow2(std::size_t x) { return std::bit_ceil(x); }

inline std::size_t bucket_mask_to_capacity(std::size_t bucket_mask)
{
  // Capacity is the maximum number of full buckets allowed before growth.
  // Cheesemap keeps at least 1/8 of the buckets empty, so capacity is 7/8
  // of the bucket count.
  return ((bucket_mask + 1) / Load_Denom) * Load_Num;
}

inline std::size_t alignup(std::size_t x, std::size_t align)
{
  assert(is_pow2(align) == true);
  return (x + align - 1) & ~(align - 1);
}

[[maybe_unused]] inline bool is_aligned(std::size_t x, std::size_t align)
{
  assert(is_pow2(align) == true);
  return (x & (align - 1)) == 0;
}

inline std::size_t capacity_to_bucket(std::size_t capacity)
{
  // Choose enough buckets to hold `capacity` items at a 7/8 max load factor.
  std::size_t adjusted_capacity = capacity * Load_Denom / Load_Num;
  return max<std::size_t>(next_pow2(adjusted_capacity), Group_Size);
}

[[maybe_unused]] inline bool is_special(std::uint8_t tag)
{
  // Returns true for special control bytes, which have their high bit set.
  // EMPTY and DELETED are special; FULL control bytes are not.
  return (tag & Ctrl_Deleted) != 0;
}

inline bool is_empty(std::uint8_t tag)
{
  assert(is_special(tag) == true);
  return (tag & Ctrl_End) != 0;
}

inline std::size_t h1(Hash hash)
{
  // Convert the hash to the native word size used by the probing logic.
  // On narrower targets this truncates the upper bits of the hash.
  return (std::size_t)hash;
}

inline std::uint8_t h2(Hash hash)
{
  // On 64-bit platforms this leaves exactly 7 bits after the shift.
  // On 32-bit platforms std::size_t is 32-bit while Hash is 64-bit, so
  // shifting by 25 leaves a 39-bit intermediate value instead.
  std::uint64_t shifted = hash >> (sizeof(std::size_t) * CHAR_BIT - Fp_Size);

  // Mask the intermediate value down to the 7 fingerprint bits stored in
  // the ctrl block.
  return (std::uint8_t)(shifted & H2_Mask);
}

/**
 *
 * Bitmask_Iter walks the set bits in a bitmask.
 * Each step returns the lowest set bit and clears it from the iterator.
 */

using Bitmask_Iter = Bitmask;

inline bool bitmask_iter_next(Bitmask_Iter* iter, std::size_t* out_index)
{
  Bitmask_Iter it = *iter;
  if (it == 0) return false;

  std::size_t bit = bitmask_trailing_zeros(it);
  it &= (it - 1);

  *iter = it;
  *out_index = bit;

  return true;
}

/**
 *
 * Full_Iter walks buckets whose control bytes are FULL.
 * Each step returns the bucket index for one occupied entry.
 */

struct Full_Iter
{
  Bitmask_Iter bitmask_iter;
  std::size_t bucket_index;
  std::size_t num_items;
  std::uint8_t const* ctrl;
};

inline Bitmask_Iter full_iter_load_mask(std::uint8_t const* ctrl)
{
  Group group = group_load(ctrl);
  return group_match_full(group);
}

inline Full_Iter full_iter_new(std::uint8_t const* ctrl, std::size_t num_items)
{
  Bitmask_Iter iter = full_iter_load_mask(ctrl);
  return Full_Iter{iter, 0, num_items, ctrl};
}

inline std::size_t full_iter_next_inner(Full_Iter* iter)
{
  Full_Iter it = *iter;

  while (true)
  {
    std::size_t group_offset;
    if (bitmask_iter_next(&it.bitmask_iter, &group_offset))
    {
      *iter = it;
      return it.bucket_index + group_offset;
    }

    it.ctrl += Group_Size;
    it.bitmask_iter = full_iter_load_mask(it.ctrl);
    it.bucket_index += Group_Size;
  }
}

inline bool full_iter_next(Full_Iter* iter, std::size_t* out_offset)
{
  std::size_t num_items = iter->num_items;
  if (num_items == 0) return false;

  *out_offset = full_iter_next_inner(iter);
  iter->num_items = num_items - 1;
  return true;
}

/**
 *
 * Probe sequences advance by triangular numbers over control groups.
 * Because the table size is always a power of two, this visits every group
 * before repeating.
 */

struct Probe_Sequence
{
  std::size_t pos;
  std::size_t stride;
};

inline void probe_sequence_next(Probe_Sequence* seq, std::size_t bucket_mask)
{
  Probe_Sequence s = *seq;
  assert(s.stride <= bucket_mask);

  // Advance by one more group than the previous step. This forms a triangular
  // probe sequence over groups:
  //
  //   step:     0   1   2   3   4
  //   stride:   0   1   2   3   4 groups
  //   offset:   0   1   3   6  10 groups from start
  //
  // Because the table has a power-of-two number of buckets, masking by
  // `bucket_mask` wraps this sequence through every group.

  s.stride += Group_Size;
  s.pos += s.stride;
  s.pos &= bucket_mask;
  *seq = s;
}

/**
 *
 * Entry stores one key/value pair in the table's entry array.
 */

template <typename K, typename V>
struct Entry
{
  K key;
  V value;
};

template <typename K, typename V>
constexpr std::size_t Entry_Size = sizeof(Entry<K, V>);

template <typename K, typename V>
constexpr std::size_t Entry_Align = alignof(Entry<K, V>);

template <typename K, typename V>
inline std::size_t layout_for(std::size_t num_buckets, std::size_t& out_ctrl_offset)
{
  assert(is_pow2(num_buckets) == true);

  // Allocate entries and control bytes in one block:
  //
  //   [entries, stored in reverse bucket order] [padding] [ctrl bytes] [ctrl
  //   clone]
  //
  // `ctrl` points at the first control byte. Entries are addressed backwards
  // from `ctrl`, so bucket 0 lives immediately before the control region and
  // bucket N - 1 lives at the start of the allocation. The extra Group_Size
  // control bytes clone the first group so group loads can wrap without a
  // branch.

  std::size_t ctrl_align = max<std::size_t>(Group_Size, Entry_Align<K, V>);

  // TODO: check for overflow

  std::size_t base_offset = Entry_Size<K, V> * num_buckets;
  std::size_t ctrl_offset = alignup(base_offset, ctrl_align);

  std::size_t total_size = ctrl_offset + num_buckets + Group_Size;
  total_size = alignup(total_size, Entry_Align<K, V>);

  out_ctrl_offset = ctrl_offset;
  return total_size;
}

}  // namespace impl

/**
 *
 * Map is a Swiss-table-style hash map.
 *
 * The map stores keys and values in a contiguous entry array and keeps probing
 * metadata in a separate control-byte array. `growth_left` tracks how many
 * more empty buckets may be filled before the table must grow.
 */

template <typename K, typename V, typename A>
struct Map
{
  std::size_t growth_left;
  std::size_t count;
  std::size_t bucket_mask;
  std::uint8_t* ctrl;
  A* allocator;
};

template <typename K, typename V, typename A>
Map<K, V, A> map_new(A* allocator)
{
  return Map<K, V, A>{0, 0, 0, (std::uint8_t*)impl::Init_Ctrl.data(), allocator};
}

template <typename K, typename V, typename A>
bool map_new_with(Map<K, V, A>* map, A* allocator, std::size_t init_capacity)
{
  std::size_t num_buckets = impl::capacity_to_bucket(init_capacity);

  std::size_t ctrl_offset;
  std::size_t total_size = impl::layout_for<K, V>(num_buckets, ctrl_offset);

  assert(total_size % impl::Entry_Align<K, V> == 0);

  std::uint8_t* entries = impl::alloc(allocator, total_size, impl::Entry_Align<K, V>);
  if (entries == nullptr)
  {
    return false;
  }
  assert(impl::is_aligned((std::size_t)entries, impl::Entry_Align<K, V>) == true);

  std::uint8_t* ctrl = entries + ctrl_offset;
  std::memset(ctrl, impl::Ctrl_Empty, num_buckets + impl::Group_Size);

  std::size_t growth_left = impl::bucket_mask_to_capacity(num_buckets - 1);
  *map = Map<K, V, A>{growth_left, 0, num_buckets - 1, ctrl, allocator};
  return true;
}

template <typename K, typename V, typename A>
void map_drop(Map<K, V, A>* map)
{
  A* allocator = map->allocator;
  if (map->ctrl == impl::Init_Ctrl.data()) return;

  std::size_t ctrl_offset;
  std::size_t total_size = impl::layout_for<K, V>(map->bucket_mask + 1, ctrl_offset);

  std::uint8_t* entries = map->ctrl - ctrl_offset;
  impl::dealloc(allocator, entries, total_size, impl::Entry_Align<K, V>);
  *map = map_new<K, V, A>(allocator);
}

namespace impl
{

template <typename K, typename V, typename A>
inline bool find_insert_index_in_group(Map<K, V, A> const* map, Group group,
                                       Probe_Sequence const* seq, std::size_t* offset)
{
  Bitmask mask = group_match_empty_or_deleted(group);
  if (mask == 0) return false;

  std::size_t lowest = bitmask_trailing_zeros(mask);
  *offset = (seq->pos + lowest) & map->bucket_mask;
  return true;
}

template <typename K, typename V, typename A>
inline std::uint8_t* ctrl_at(Map<K, V, A> const* map, std::size_t index)
{
  assert(index < map->bucket_mask + 1);
  return map->ctrl + index;
}

template <typename K, typename V, typename A>
inline std::size_t find_insert_index(Map<K, V, A> const* map, std::size_t h1)
{
  std::size_t bucket_mask = map->bucket_mask;
  auto seq = Probe_Sequence{
      h1 & bucket_mask,
      0,
  };

  while (true)
  {
    std::uint8_t* ctrl = ctrl_at(map, seq.pos);
    Group group = group_load(ctrl);

    std::size_t offset;
    if (find_insert_index_in_group(map, group, &seq, &offset))
    {
      return offset;
    }

    probe_sequence_next(&seq, bucket_mask);
  }
}

template <typename K, typename V, typename A>
Entry<K, V>* entry_at(Map<K, V, A> const* map, std::size_t index)
{
  assert(map->bucket_mask != 0);
  assert(index < map->bucket_mask + 1);

  auto end = (Entry<K, V>*)map->ctrl;
  return end - index - 1;
}

template <typename K, typename V, typename A>
void ctrl_set(Map<K, V, A>* map, std::size_t index, std::uint8_t tag)
{
  std::size_t index2 = ((index - Group_Size) & map->bucket_mask) + Group_Size;

  map->ctrl[index] = tag;
  map->ctrl[index2] = tag;
}

template <typename K, typename V, typename A>
void insert_at(Map<K, V, A>* map, std::size_t index, std::uint8_t tag, Entry<K, V> const* entry)
{
  std::uint8_t old_ctrl = map->ctrl[index];
  map->growth_left -= (std::size_t)is_empty(old_ctrl);
  ctrl_set(map, index, tag);
  map->count += 1;

  auto at = entry_at(map, index);
  *at = *entry;
}

template <typename K, typename V, typename A>
bool resize(Map<K, V, A>* map, std::size_t new_capacity)
{
  Map<K, V, A> new_map = {};
  if (!map_new_with(&new_map, map->allocator, new_capacity))
  {
    return false;
  }

  Full_Iter iter = full_iter_new(map->ctrl, map->count);
  std::size_t ctrl_offset;

  while (full_iter_next(&iter, &ctrl_offset))
  {
    auto* src = entry_at(map, ctrl_offset);
    Hash h = hash(src->key);

    std::size_t insert_index = find_insert_index(&new_map, h1(h));
    ctrl_set(&new_map, insert_index, h2(h));

    auto* dest = entry_at(&new_map, insert_index);
    std::memcpy(dest, src, Entry_Size<K, V>);
  }

  new_map.count = map->count;
  new_map.growth_left -= map->count;

  map_drop(map);
  *map = new_map;
  return true;
}

template <typename K, typename V, typename A>
inline bool find(Map<K, V, A> const* map, K key, std::size_t h1, std::uint8_t h2,
                 std::size_t* out_index)
{
  std::size_t bucket_mask = map->bucket_mask;
  auto seq = Probe_Sequence{
      h1 & bucket_mask,
      0,
  };

  while (true)
  {
    std::uint8_t* ctrl = ctrl_at(map, seq.pos);
    Group group = group_load(ctrl);

    Bitmask_Iter match_mask = group_match_tag(group, h2);
    std::size_t bit;

    while (bitmask_iter_next(&match_mask, &bit))
    {
      std::size_t index = (seq.pos + bit) & bucket_mask;

      auto entry = entry_at(map, index);
      if (compare(key, entry->key))
      {
        *out_index = index;
        return true;
      }
    }

    if (group_match_empty(group) != 0)
    {
      return false;
    }

    probe_sequence_next(&seq, bucket_mask);
  }
}

template <typename K, typename V, typename A>
inline bool find_or_find_insert(Map<K, V, A> const* map, K key, std::size_t h1, std::uint8_t h2,
                                std::size_t* insert_index)
{
  bool has_insert_index = false;
  std::size_t bucket_mask = map->bucket_mask;
  auto seq = Probe_Sequence{
      h1 & bucket_mask,
      0,
  };

  while (true)
  {
    std::uint8_t* ctrl = ctrl_at(map, seq.pos);
    Group group = group_load(ctrl);

    Bitmask_Iter match_iter = group_match_tag(group, h2);
    std::size_t bit;

    // Check every slot in this group whose H2 fingerprint matches `h2`.
    // Fingerprints are only a fast filter, so each candidate still needs a
    // full key comparison before it can be reported as found. When no
    // candidate matches, probing continues below and the first available
    // empty/deleted slot is remembered as the possible insertion point.
    while (bitmask_iter_next(&match_iter, &bit))
    {
      std::size_t index = (seq.pos + bit) & bucket_mask;

      auto entry = entry_at(map, index);
      if (compare(key, entry->key))
      {
        *insert_index = index;
        return true;
      }
    }

    if (!has_insert_index)
    {
      has_insert_index = find_insert_index_in_group(map, group, &seq, insert_index);
    }

    if (has_insert_index && group_match_empty(group) != 0)
    {
      return false;
    }

    probe_sequence_next(&seq, bucket_mask);
  }
}

}  // namespace impl

template <typename K, typename V, typename A>
void map_shrink_to_fit(Map<K, V, A>* map)
{
  // Shrink to fit recalculates capacity based on current item count.
  // The minimum capacity is 1 because map_new_with always allocates
  // at least Group_Size buckets, ensuring we never have zero capacity.
  // Infact it doesn't matter whether we take the max with 1 or Group_Size.
  std::size_t new_capacity = impl::max<std::size_t>(map->count, 1);
  if (new_capacity >= impl::bucket_mask_to_capacity(map->bucket_mask))
  {
    return;
  }

  // Shrinking is best-effort: a failed reallocation leaves the existing table
  // untouched, so we keep the current map and report nothing.
  (void)impl::resize(map, new_capacity);
}

template <typename K, typename V, typename A>
bool map_reserve(Map<K, V, A>* map, std::size_t additional)
{
  // growth_left is the remaining insertion budget before the table must
  // grow. DELETED tombstones spend this budget without raising count, so
  // the resize decision must use growth_left, not count.
  if (additional <= map->growth_left)
  {
    return true;
  }

  // TODO: check overflow
  std::size_t min_capacity = map->count + additional;
  std::size_t total_capacity = impl::bucket_mask_to_capacity(map->bucket_mask);
  // TODO: check for rehash if we have plenty of space left

  return impl::resize(map, impl::max<std::size_t>(min_capacity, total_capacity + 1));
}

template <typename K, typename V, typename A>
bool map_lookup(Map<K, V, A> const* map, K key, V* out_value)
{
  Hash h = impl::hash(key);
  std::size_t h1_val = impl::h1(h);
  std::uint8_t h2_val = impl::h2(h);

  std::size_t index;
  if (impl::find(map, key, h1_val, h2_val, &index))
  {
    auto entry = impl::entry_at(map, index);
    *out_value = entry->value;
    return true;
  }

  return false;
}

template <typename K, typename V, typename A>
bool map_insert(Map<K, V, A>* map, K key, V value)
{
  Hash h = impl::hash(key);
  std::size_t h1_val = impl::h1(h);
  std::uint8_t h2_val = impl::h2(h);

  std::size_t insert_index;
  if (impl::find_or_find_insert(map, key, h1_val, h2_val, &insert_index))
  {
    auto entry = impl::entry_at(map, insert_index);
    entry->value = value;
    return true;
  }

  if (map->growth_left == 0 && impl::is_empty(map->ctrl[insert_index]))
  {
    if (!map_reserve(map, 1))
    {
      return false;
    }

    insert_index = impl::find_insert_index(map, h1_val);
  }

  impl::Entry entry{key, value};
  impl::insert_at(map, insert_index, h2_val, &entry);
  return true;
}

template <typename K, typename V, typename A>
bool map_remove(Map<K, V, A>* map, K key)
{
  Hash h = impl::hash(key);
  std::size_t index;
  if (!impl::find(map, key, impl::h1(h), impl::h2(h), &index))
  {
    return false;
  }
  std::size_t index_before = (index - impl::Group_Size) & map->bucket_mask;

  // We can't always mark a removed slot EMPTY. Lookup stops probing at EMPTY,
  // so clearing a slot in the middle of a probe chain could make displaced
  // entries unreachable.
  //
  // To decide whether the slot can become EMPTY, we examine the surrounding
  // control bytes. `bitmask_leading_zeros(empty_before)` counts the contiguous
  // non-EMPTY bytes ending at the previous group, while
  // `bitmask_trailing_zeros(empty_after)` counts the contiguous non-EMPTY bytes
  // starting at the current group.
  //
  // If the combined span is at least one full group wide, then this slot may
  // still be part of an active probe chain and must remain DELETED so probing
  // continues through it.
  //
  // Otherwise there is already an EMPTY nearby, meaning lookups would terminate
  // naturally anyway, so we can safely convert this slot back to EMPTY and
  // restore one growth slot.

  impl::Group group_before = impl::group_load(impl::ctrl_at(map, index_before));
  impl::Group group_after = impl::group_load(impl::ctrl_at(map, index));
  impl::Bitmask empty_before = impl::group_match_empty(group_before);
  impl::Bitmask empty_after = impl::group_match_empty(group_after);

  std::size_t num_zeros =
      impl::bitmask_leading_zeros(empty_before) + impl::bitmask_trailing_zeros(empty_after);

  if (num_zeros >= impl::Group_Size)
  {
    impl::ctrl_set(map, index, impl::Ctrl_Deleted);
  }
  else
  {
    impl::ctrl_set(map, index, impl::Ctrl_Empty);
    map->growth_left += 1;
  }

  map->count -= 1;
  return true;
}

/**
 *
 * Iterator for occupied Map entries.
 *
 * Iteration follows bucket order, skipping EMPTY and DELETED slots. Each call
 * to map_iter_next returns pointers to the stored key and value. The key
 * pointer is const because changing a key in place would break the table's hash
 * invariant.
 */

template <typename K, typename V, typename A>
struct Map_Iter
{
  impl::Full_Iter full_iter;
  Map<K, V, A>* map;
};

template <typename K, typename V, typename A>
Map_Iter<K, V, A> map_iter_new(Map<K, V, A>* map)
{
  return Map_Iter<K, V, A>{impl::full_iter_new(map->ctrl, map->count), map};
}

template <typename K, typename V, typename A>
bool map_iter_next(Map_Iter<K, V, A>* iter, K const** out_key, V** out_value)
{
  std::size_t offset;

  if (!impl::full_iter_next(&iter->full_iter, &offset))
  {
    return false;
  }

  auto entry = impl::entry_at(iter->map, offset);
  *out_key = &entry->key;
  *out_value = &entry->value;

  return true;
}

struct Unit
{};

static_assert(sizeof(Unit) == 1, "Unit must be exactly one byte");

/**
 *
 * Set is a Swiss-table-style hash set backed by Map.
 *
 * The set stores keys in the backing map and uses Unit as the value. This gives
 * Set the same probing, allocation, resizing, and removal behavior as Map while
 * exposing only membership operations.
 */

template <typename K, typename A>
struct Set
{
  Map<K, Unit, A> map;
};

template <typename K, typename A>
Set<K, A> set_new(A* allocator)
{
  return Set<K, A>{map_new<K, Unit, A>(allocator)};
}

template <typename K, typename A>
bool set_new_with(Set<K, A>* set, A* allocator, std::size_t init_capacity)
{
  return map_new_with(&set->map, allocator, init_capacity);
}

template <typename K, typename A>
void set_drop(Set<K, A>* set)
{
  map_drop(&set->map);
}

template <typename K, typename A>
bool set_insert(Set<K, A>* set, K key)
{
  return map_insert(&set->map, key, Unit{});
}

template <typename K, typename A>
bool set_lookup(Set<K, A> const* set, K key)
{
  Unit unit;
  return map_lookup(&set->map, key, &unit);
}

template <typename K, typename A>
bool set_remove(Set<K, A>* set, K key)
{
  return map_remove(&set->map, key);
}

/**
 *
 * Iterator for occupied Set entries.
 *
 * Iteration follows bucket order, skipping EMPTY and DELETED slots. Each call
 * to set_iter_next returns a pointer to the stored key. The key pointer is
 * const because changing a key in place would break the table's hash invariant.
 */

template <typename K, typename A>
struct Set_Iter
{
  Map_Iter<K, Unit, A> map_iter;
};

template <typename K, typename A>
Set_Iter<K, A> set_iter_new(Set<K, A>* set)
{
  return Set_Iter<K, A>{map_iter_new(&set->map)};
}

template <typename K, typename A>
bool set_iter_next(Set_Iter<K, A>* iter, K const** out_key)
{
  K const* key;
  Unit* value;

  if (!map_iter_next(&iter->map_iter, &key, &value))
  {
    return false;
  }

  *out_key = key;
  return true;
}

}  // namespace cheesemap
