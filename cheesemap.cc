#pragma once

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(__cplusplus)
#error "Cheesemap requires C++"
#endif

#if __cplusplus < 201703L
#error "Cheesemap requires C++17 or later"
#endif

#if defined(_MSC_VER)
#error "MSVC is not yet supported. Open an Issue if you need this."
#endif

#define CM_MAX(a, b) ((a) > (b) ? (a) : (b))

#define CM_REPEAT_1(x) x
#define CM_REPEAT_2(x) CM_REPEAT_1(x), CM_REPEAT_1(x)
#define CM_REPEAT_4(x) CM_REPEAT_2(x), CM_REPEAT_2(x)
#define CM_REPEAT_8(x) CM_REPEAT_4(x), CM_REPEAT_4(x)
#define CM_REPEAT_16(x) CM_REPEAT_8(x), CM_REPEAT_8(x)

#if defined(__SSE2__)
#include <emmintrin.h>

#define CM_GROUP_SIZE 16
#define CM_BITMASK_STRIDE 1
#define CM_IS_SIMD
#endif

#if !defined(CM_IS_SIMD)
#define CM_GROUP_SIZE __SIZEOF_POINTER__
#define CM_BITMASK_STRIDE CHAR_BIT
#endif

#define CM_ENTRY_USE Entry<K, V>

/**
 *
 * Template parameter macros used to keep the public and internal function
 * signatures short.
 *
 * K: key type
 * V: value type
 * Hasher: function pointer that hashes a key
 * Comparer: function pointer that compares two keys for equality
 *
 * Allocation is supplied at runtime through an IAllocator passed by value to
 * each method that allocates or deallocates.
 */

#define CM_TEMPLATE template <typename K, typename V, Hash_Fn<K> Hasher, Compare_Fn<K> Comparer>
#define CM_TEMPLATE_USE K, V, Hasher, Comparer

/**
 *
 * Template parameter macros used to keep the public Set function
 * signatures short.
 *
 * K: key type
 * Hasher: function pointer that hashes a key
 * Comparer: function pointer that compares two keys for equality
 */

#define CM_CS_TEMPLATE template <typename K, Hash_Fn<K> Hasher, Compare_Fn<K> Comparer>
#define CM_CS_TEMPLATE_USE K, Hasher, Comparer
#define CM_CS_INNER_TEMPLATE_USE K, Unit, Hasher, Comparer

namespace cheesemap
{

/**
 *
 * Hash and compare operations
 */

using Hash = uint64_t;

template <typename K>
using Hash_Fn = Hash (*)(K key);

template <typename K>
using Compare_Fn = bool (*)(K key0, K key1);

using Alloc_Fn = uint8_t* (*)(uint8_t* ctx, size_t size, size_t align);

using Dealloc_Fn = void (*)(uint8_t* ctx, uint8_t* ptr, size_t size, size_t align);

struct IAllocator
{
  uint8_t* ctx;
  Alloc_Fn alloc;
  Dealloc_Fn dealloc;
};

/**
 *
 * Control-byte encoding.
 *
 * Each bucket has one control byte. EMPTY and DELETED are special states with
 * the high bit set. FULL buckets store the 7-bit H2 hash fingerprint with the
 * high bit clear, which lets group matching test many buckets at once.
 *
 * The control array also stores a cloned prefix of CM_GROUP_SIZE bytes after
 * the real buckets, so group loads can wrap around the end of the table.
 */

enum : uint8_t
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
  Word_Width = sizeof(size_t) * CHAR_BIT,
};

#if defined(__SSE2__)
using Group = __m128i;
using Bitmask = uint16_t;
#endif

#if !defined(CM_IS_SIMD)
using Group = size_t;
using Bitmask = Group;
#endif

/**
 *
 * Group actions interface
 */

// TODO: check whether passing my pointer is faster
inline Group group_load(const uint8_t* ctrl);
inline Bitmask group_match_tag(Group group, uint8_t tag);
inline Bitmask group_match_empty_or_deleted(Group group);
inline Bitmask group_match_empty(Group group);
inline Bitmask group_match_full(Group group);

/**
 *
 * SSE2 implementation of the group actions
 */

#if defined(__SSE2__)
inline Group group_load(const uint8_t* ctrl) { return _mm_loadu_si128((const Group*)ctrl); }

inline Bitmask group_match_tag(Group group, uint8_t tag)
{
  const __m128i tagvec = _mm_set1_epi8(tag);
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
#endif

/**
 *
 * Scalar implementation of the group actions
 */

#if !defined(CM_IS_SIMD)
inline Group group_repeat(uint8_t v) { return (Group)v * (((Group)-1) / (uint8_t)~0); }

inline Group group_load(const uint8_t* ctrl)
{
  assert(ctrl != NULL);

  Group v;
  memcpy(&v, ctrl, sizeof(v));
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

inline Bitmask group_match_tag(Group group, uint8_t tag)
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

inline constexpr uint8_t Init_Ctrl[CM_GROUP_SIZE] = {
#if CM_GROUP_SIZE == 16
    CM_REPEAT_16(Ctrl_Empty)
#elif CM_GROUP_SIZE == 8
    CM_REPEAT_8(Ctrl_Empty)
#elif CM_GROUP_SIZE == 4
    CM_REPEAT_4(Ctrl_Empty)
#else
#error "group size is not supported"
#endif
};

/**
 *
 * Return the number of trailing zero bits in a bitmask.
 * Returns CM_GROUP_SIZE when the mask is zero.
 */

inline uint32_t bitmask_trailing_zeros(Bitmask mask)
{
  if (mask == 0)
  {
    return CM_GROUP_SIZE;
  }

#if defined(__x86_64__)
  return __builtin_ctzll(mask) / CM_BITMASK_STRIDE;
#elif defined(__i386__)
  return __builtin_ctz(mask) / CM_BITMASK_STRIDE;
#else
#error "target platform is not supported"
#endif
}

/**
 *
 * Return the number of leading zero bits.
 * Returns Word_Width when x is zero.
 */

inline uint32_t leading_zeros(size_t x)
{
  if (x == 0)
  {
    return Word_Width;
  }

#if defined(__x86_64__)
  return __builtin_clzll(x);
#elif defined(__i386__)
  return __builtin_clz(x);
#else
#error "target platform is not supported"
#endif
}

inline uint32_t bitmask_leading_zeros(Bitmask mask)
{
  // Must return slot units, like bitmask_trailing_zeros.
#if CM_BITMASK_STRIDE == 1
  // Dense bitmask, one bit per slot
  return leading_zeros(mask) - (Word_Width - CM_GROUP_SIZE);
#else
  // SWAR bitmask, one byte per slot
  return leading_zeros(mask) / CM_BITMASK_STRIDE;
#endif
}

[[maybe_unused]] inline bool is_pow2(size_t x) { return x != 0 && (x & (x - 1)) == 0; }

inline size_t next_pow2(size_t x)
{
  if (x <= 1) return 1;

  return ((size_t)1 << (Word_Width - leading_zeros(x - 1)));
}

inline size_t bucket_mask_to_capacity(size_t bucket_mask)
{
  // Capacity is the maximum number of full buckets allowed before growth.
  // Cheesemap keeps at least 1/8 of the buckets empty, so capacity is 7/8
  // of the bucket count.
  return ((bucket_mask + 1) / Load_Denom) * Load_Num;
}

inline size_t alignup(size_t x, size_t align)
{
  assert(is_pow2(align) == true);
  return (x + align - 1) & ~(align - 1);
}

[[maybe_unused]] inline bool is_aligned(size_t x, size_t align)
{
  assert(is_pow2(align) == true);
  return (x & (align - 1)) == 0;
}

inline size_t capacity_to_bucket(size_t capacity)
{
  // Choose enough buckets to hold `capacity` items at a 7/8 max load factor.
  size_t adjusted_capacity = capacity * Load_Denom / Load_Num;
  return CM_MAX(next_pow2(adjusted_capacity), CM_GROUP_SIZE);
}

[[maybe_unused]] inline bool is_special(uint8_t tag)
{
  // Returns true for special control bytes, which have their high bit set.
  // EMPTY and DELETED are special; FULL control bytes are not.
  return (tag & Ctrl_Deleted) != 0;
}

inline bool is_empty(uint8_t tag)
{
  assert(is_special(tag) == true);
  return (tag & Ctrl_End) != 0;
}

inline size_t h1(Hash hash)
{
  // Convert the hash to the native word size used by the probing logic.
  // On narrower targets this truncates the upper bits of the hash.
  return (size_t)hash;
}

inline uint8_t h2(Hash hash)
{
  // On 64-bit platforms this leaves exactly 7 bits after the shift.
  // On 32-bit platforms size_t is 32-bit while Hash is 64-bit, so
  // shifting by 25 leaves a 39-bit intermediate value instead.
  uint64_t shifted = hash >> (sizeof(size_t) * CHAR_BIT - Fp_Size);

  // Mask the intermediate value down to the 7 fingerprint bits stored in
  // the ctrl block.
  return (uint8_t)(shifted & H2_Mask);
}

/**
 *
 * Bitmask_Iter walks the set bits in a bitmask.
 * Each step returns the lowest set bit and clears it from the iterator.
 */

using Bitmask_Iter = Bitmask;

inline bool bitmask_iter_next(Bitmask_Iter* iter, size_t* out_index)
{
  Bitmask_Iter it = *iter;
  if (it == 0) return false;

  size_t bit = bitmask_trailing_zeros(it);
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
  size_t bucket_index;
  size_t num_items;
  uint8_t const* ctrl;
};

inline Bitmask_Iter full_iter_load_mask(uint8_t const* ctrl)
{
  Group group = group_load(ctrl);
  return group_match_full(group);
}

inline Full_Iter full_iter_new(uint8_t const* ctrl, size_t num_items)
{
  Bitmask_Iter iter = full_iter_load_mask(ctrl);
  return Full_Iter{iter, 0, num_items, ctrl};
}

inline size_t full_iter_next_inner(Full_Iter* iter)
{
  Full_Iter it = *iter;

  while (true)
  {
    size_t group_offset;
    if (bitmask_iter_next(&it.bitmask_iter, &group_offset))
    {
      *iter = it;
      return it.bucket_index + group_offset;
    }

    it.ctrl += CM_GROUP_SIZE;
    it.bitmask_iter = full_iter_load_mask(it.ctrl);
    it.bucket_index += CM_GROUP_SIZE;
  }
}

inline bool full_iter_next(Full_Iter* iter, size_t* out_offset)
{
  size_t num_items = iter->num_items;
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
  size_t pos;
  size_t stride;
};

inline void probe_sequence_next(Probe_Sequence* seq, size_t bucket_mask)
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

  s.stride += CM_GROUP_SIZE;
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
inline Entry<K, V> entry_new(K key, V value)
{
  return Entry<K, V>{key, value};
}

/**
 *
 * Map is a Swiss-table-style hash map.
 *
 * The map stores keys and values in a contiguous entry array and keeps probing
 * metadata in a separate control-byte array. `growth_left` tracks how many
 * more empty buckets may be filled before the table must grow.
 */

CM_TEMPLATE
struct Map
{
  size_t growth_left;
  size_t count;
  size_t bucket_mask;
  uint8_t* ctrl;
};

CM_TEMPLATE
Map<CM_TEMPLATE_USE> map_new() { return Map<CM_TEMPLATE_USE>{0, 0, 0, (uint8_t*)Init_Ctrl}; }

CM_TEMPLATE
inline size_t layout_for(size_t num_buckets, size_t& out_ctrl_offset)
{
  assert(is_pow2(num_buckets) == true);

  // Allocate entries and control bytes in one block:
  //
  //   [entries, stored in reverse bucket order] [padding] [ctrl bytes] [ctrl
  //   clone]
  //
  // `ctrl` points at the first control byte. Entries are addressed backwards
  // from `ctrl`, so bucket 0 lives immediately before the control region and
  // bucket N - 1 lives at the start of the allocation. The extra CM_GROUP_SIZE
  // control bytes clone the first group so group loads can wrap without a
  // branch.

  size_t ctrl_align = CM_MAX(CM_GROUP_SIZE, alignof(CM_ENTRY_USE));

  // TODO: check for overflow

  size_t base_offset = sizeof(CM_ENTRY_USE) * num_buckets;
  size_t ctrl_offset = alignup(base_offset, ctrl_align);

  size_t total_size = ctrl_offset + num_buckets + CM_GROUP_SIZE;
  total_size = alignup(total_size, alignof(CM_ENTRY_USE));

  out_ctrl_offset = ctrl_offset;
  return total_size;
}

CM_TEMPLATE
bool map_new_with(Map<CM_TEMPLATE_USE>* map, IAllocator allocator, size_t init_capacity)
{
  size_t num_buckets = capacity_to_bucket(init_capacity);

  size_t ctrl_offset;
  size_t total_size = layout_for<CM_TEMPLATE_USE>(num_buckets, ctrl_offset);

  assert(total_size % alignof(CM_ENTRY_USE) == 0);

  uint8_t* entries = allocator.alloc(allocator.ctx, total_size, alignof(CM_ENTRY_USE));
  if (entries == NULL)
  {
    return false;
  }
  assert(is_aligned((size_t)entries, alignof(CM_ENTRY_USE)) == true);

  uint8_t* ctrl = entries + ctrl_offset;
  memset(ctrl, Ctrl_Empty, num_buckets + CM_GROUP_SIZE);

  size_t growth_left = bucket_mask_to_capacity(num_buckets - 1);
  *map = Map<CM_TEMPLATE_USE>{growth_left, 0, num_buckets - 1, ctrl};
  return true;
}

CM_TEMPLATE
void map_drop(Map<CM_TEMPLATE_USE>* map, IAllocator allocator)
{
  if (map->ctrl == Init_Ctrl) return;

  size_t ctrl_offset;
  size_t total_size = layout_for<CM_TEMPLATE_USE>(map->bucket_mask + 1, ctrl_offset);

  uint8_t* entries = map->ctrl - ctrl_offset;
  allocator.dealloc(allocator.ctx, entries, total_size, alignof(CM_ENTRY_USE));
  *map = map_new<CM_TEMPLATE_USE>();
}

CM_TEMPLATE
inline bool find_insert_index_in_group(const Map<CM_TEMPLATE_USE>* map, Group group,
                                       const Probe_Sequence* seq, size_t* offset)
{
  Bitmask mask = group_match_empty_or_deleted(group);
  if (mask == 0) return false;

  size_t lowest = bitmask_trailing_zeros(mask);
  *offset = (seq->pos + lowest) & map->bucket_mask;
  return true;
}

CM_TEMPLATE
inline uint8_t* ctrl_at(const Map<CM_TEMPLATE_USE>* map, size_t index)
{
  assert(index < map->bucket_mask + 1);
  return map->ctrl + index;
}

CM_TEMPLATE
inline size_t find_insert_index(const Map<CM_TEMPLATE_USE>* map, size_t h1)
{
  size_t bucket_mask = map->bucket_mask;
  auto seq = Probe_Sequence{
      h1 & bucket_mask,
      0,
  };

  while (true)
  {
    uint8_t* ctrl = ctrl_at(map, seq.pos);
    Group group = group_load(ctrl);

    size_t offset;
    if (find_insert_index_in_group(map, group, &seq, &offset))
    {
      return offset;
    }

    probe_sequence_next(&seq, bucket_mask);
  }
}

CM_TEMPLATE
CM_ENTRY_USE* entry_at(const Map<CM_TEMPLATE_USE>* map, size_t index)
{
  assert(map->bucket_mask != 0);
  assert(index < map->bucket_mask + 1);

  auto end = (CM_ENTRY_USE*)map->ctrl;
  return end - index - 1;
}

CM_TEMPLATE
void ctrl_set(Map<CM_TEMPLATE_USE>* map, size_t index, uint8_t tag)
{
  size_t index2 = ((index - CM_GROUP_SIZE) & map->bucket_mask) + CM_GROUP_SIZE;

  map->ctrl[index] = tag;
  map->ctrl[index2] = tag;
}

CM_TEMPLATE
void insert_at(Map<CM_TEMPLATE_USE>* map, size_t index, uint8_t tag, const CM_ENTRY_USE* entry)
{
  uint8_t old_ctrl = map->ctrl[index];
  map->growth_left -= (size_t)is_empty(old_ctrl);
  ctrl_set(map, index, tag);
  map->count += 1;

  auto at = entry_at(map, index);
  *at = *entry;
}

CM_TEMPLATE
bool resize(Map<CM_TEMPLATE_USE>* map, IAllocator allocator, size_t new_capacity)
{
  Map<CM_TEMPLATE_USE> new_map = map_new<CM_TEMPLATE_USE>();
  if (!map_new_with(&new_map, allocator, new_capacity))
  {
    return false;
  }

  Full_Iter iter = full_iter_new(map->ctrl, map->count);
  size_t ctrl_offset;

  while (full_iter_next(&iter, &ctrl_offset))
  {
    CM_ENTRY_USE* src = entry_at(map, ctrl_offset);
    Hash hash = Hasher(src->key);

    size_t insert_index = find_insert_index(&new_map, h1(hash));
    ctrl_set(&new_map, insert_index, h2(hash));

    CM_ENTRY_USE* dest = entry_at(&new_map, insert_index);
    memcpy(dest, src, sizeof(CM_ENTRY_USE));
  }

  new_map.count = map->count;
  new_map.growth_left -= map->count;

  map_drop(map, allocator);
  *map = new_map;
  return true;
}

CM_TEMPLATE
void map_shrink_to_fit(Map<CM_TEMPLATE_USE>* map, IAllocator allocator)
{
  // Shrink to fit recalculates capacity based on current item count.
  // The minimum capacity is 1 because map_new_with always allocates
  // at least CM_GROUP_SIZE buckets, ensuring we never have zero capacity.
  // Infact it doesn't matter whether we take the max with 1 or CM_GROUP_SIZE.
  size_t new_capacity = CM_MAX(map->count, 1);
  if (new_capacity >= bucket_mask_to_capacity(map->bucket_mask))
  {
    return;
  }

  // Shrinking is best-effort: a failed reallocation leaves the existing table
  // untouched, so we keep the current map and report nothing.
  (void)resize(map, allocator, new_capacity);
}

CM_TEMPLATE
bool map_reserve(Map<CM_TEMPLATE_USE>* map, IAllocator allocator, size_t additional)
{
  // growth_left is the remaining insertion budget before the table must
  // grow. DELETED tombstones spend this budget without raising count, so
  // the resize decision must use growth_left, not count.
  if (additional <= map->growth_left)
  {
    return true;
  }

  // TODO: check overflow
  size_t min_capacity = map->count + additional;
  size_t total_capacity = bucket_mask_to_capacity(map->bucket_mask);
  // TODO: check for rehash if we have plenty of space left

  return resize(map, allocator, CM_MAX(min_capacity, total_capacity + 1));
}

CM_TEMPLATE
inline bool find(const Map<CM_TEMPLATE_USE>* map, K key, size_t h1, uint8_t h2, size_t* out_index)
{
  size_t bucket_mask = map->bucket_mask;
  auto seq = Probe_Sequence{
      h1 & bucket_mask,
      0,
  };

  while (true)
  {
    uint8_t* ctrl = ctrl_at(map, seq.pos);
    Group group = group_load(ctrl);

    Bitmask_Iter match_mask = group_match_tag(group, h2);
    size_t bit;

    while (bitmask_iter_next(&match_mask, &bit))
    {
      size_t index = (seq.pos + bit) & bucket_mask;

      auto entry = entry_at(map, index);
      if (Comparer(key, entry->key))
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

CM_TEMPLATE
bool map_lookup(const Map<CM_TEMPLATE_USE>* map, K key, V* out_value)
{
  Hash hash = Hasher(key);
  size_t h1_val = h1(hash);
  uint8_t h2_val = h2(hash);

  size_t index;
  if (find(map, key, h1_val, h2_val, &index))
  {
    auto entry = entry_at(map, index);
    *out_value = entry->value;
    return true;
  }

  return false;
}

CM_TEMPLATE
inline bool find_or_find_insert(const Map<CM_TEMPLATE_USE>* map, K key, size_t h1, uint8_t h2,
                                size_t* insert_index)
{
  bool has_insert_index = false;
  size_t bucket_mask = map->bucket_mask;
  auto seq = Probe_Sequence{
      h1 & bucket_mask,
      0,
  };

  while (true)
  {
    uint8_t* ctrl = ctrl_at(map, seq.pos);
    Group group = group_load(ctrl);

    Bitmask_Iter match_iter = group_match_tag(group, h2);
    size_t bit;

    // Check every slot in this group whose H2 fingerprint matches `h2`.
    // Fingerprints are only a fast filter, so each candidate still needs a
    // full key comparison before it can be reported as found. When no
    // candidate matches, probing continues below and the first available
    // empty/deleted slot is remembered as the possible insertion point.
    while (bitmask_iter_next(&match_iter, &bit))
    {
      size_t index = (seq.pos + bit) & bucket_mask;

      auto entry = entry_at(map, index);
      if (Comparer(key, entry->key))
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

CM_TEMPLATE bool map_insert(Map<CM_TEMPLATE_USE>* map, IAllocator allocator, K key, V value)
{
  Hash hash = Hasher(key);
  size_t h1_val = h1(hash);
  uint8_t h2_val = h2(hash);

  size_t insert_index;
  if (find_or_find_insert(map, key, h1_val, h2_val, &insert_index))
  {
    auto entry = entry_at(map, insert_index);
    entry->value = value;
    return true;
  }

  if (map->growth_left == 0 && is_empty(map->ctrl[insert_index]))
  {
    if (!map_reserve(map, allocator, 1))
    {
      return false;
    }

    insert_index = find_insert_index(map, h1_val);
  }

  CM_ENTRY_USE entry = entry_new(key, value);
  insert_at(map, insert_index, h2_val, &entry);
  return true;
}

CM_TEMPLATE
bool map_remove(Map<CM_TEMPLATE_USE>* map, K key)
{
  Hash hash = Hasher(key);
  size_t index;
  if (!find(map, key, h1(hash), h2(hash), &index))
  {
    return false;
  }
  size_t index_before = (index - CM_GROUP_SIZE) & map->bucket_mask;

  // We can't always mark a removed slot EMPTY. Lookup stops probing at EMPTY,
  // so clearing a slot in the middle of a probe chain could make displaced
  // entries unreachable.
  //
  // To decide whether the slot can become EMPTY, we examine the surrounding
  // control bytes. `leading_zeros(empty_before)` counts the contiguous
  // non-EMPTY bytes ending at the previous group, while
  // `trailing_zeros(empty_after)` counts the contiguous non-EMPTY bytes
  // starting at the current group.
  //
  // If the combined span is at least one full group wide, then this slot may
  // still be part of an active probe chain and must remain DELETED so probing
  // continues through it.
  //
  // Otherwise there is already an EMPTY nearby, meaning lookups would terminate
  // naturally anyway, so we can safely convert this slot back to EMPTY and
  // restore one growth slot.

  Group group_before = group_load(ctrl_at(map, index_before));
  Group group_after = group_load(ctrl_at(map, index));
  Bitmask empty_before = group_match_empty(group_before);
  Bitmask empty_after = group_match_empty(group_after);

  size_t num_zeros = bitmask_leading_zeros(empty_before) + bitmask_trailing_zeros(empty_after);

  if (num_zeros >= CM_GROUP_SIZE)
  {
    ctrl_set(map, index, Ctrl_Deleted);
  }
  else
  {
    ctrl_set(map, index, Ctrl_Empty);
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

CM_TEMPLATE
struct Map_Iter
{
  Full_Iter full_iter;
  Map<CM_TEMPLATE_USE>* map;
};

CM_TEMPLATE
Map_Iter<CM_TEMPLATE_USE> map_iter_new(Map<CM_TEMPLATE_USE>* map)
{
  return Map_Iter<CM_TEMPLATE_USE>{full_iter_new(map->ctrl, map->count), map};
}

CM_TEMPLATE
bool map_iter_next(Map_Iter<CM_TEMPLATE_USE>* iter, K const** out_key, V** out_value)
{
  size_t offset;

  if (!full_iter_next(&iter->full_iter, &offset))
  {
    return false;
  }

  auto entry = entry_at(iter->map, offset);
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

CM_CS_TEMPLATE
struct Set
{
  Map<CM_CS_INNER_TEMPLATE_USE> map;
};

CM_CS_TEMPLATE
Set<CM_CS_TEMPLATE_USE> set_new()
{
  return Set<CM_CS_TEMPLATE_USE>{map_new<CM_CS_INNER_TEMPLATE_USE>()};
}

CM_CS_TEMPLATE
bool set_new_with(Set<CM_CS_TEMPLATE_USE>* set, IAllocator allocator, size_t init_capacity)
{
  return map_new_with(&set->map, allocator, init_capacity);
}

CM_CS_TEMPLATE
void set_drop(Set<CM_CS_TEMPLATE_USE>* set, IAllocator allocator)
{
  map_drop(&set->map, allocator);
}

CM_CS_TEMPLATE
bool set_insert(Set<CM_CS_TEMPLATE_USE>* set, IAllocator allocator, K key)
{
  return map_insert(&set->map, allocator, key, Unit{});
}

CM_CS_TEMPLATE
bool set_lookup(const Set<CM_CS_TEMPLATE_USE>* set, K key)
{
  Unit unit;
  return map_lookup(&set->map, key, &unit);
}

CM_CS_TEMPLATE
bool set_remove(Set<CM_CS_TEMPLATE_USE>* set, K key) { return map_remove(&set->map, key); }

/**
 *
 * Iterator for occupied Set entries.
 *
 * Iteration follows bucket order, skipping EMPTY and DELETED slots. Each call
 * to set_iter_next returns a pointer to the stored key. The key pointer is
 * const because changing a key in place would break the table's hash invariant.
 */

CM_CS_TEMPLATE
struct Set_Iter
{
  Map_Iter<CM_CS_INNER_TEMPLATE_USE> map_iter;
};

CM_CS_TEMPLATE
Set_Iter<CM_CS_TEMPLATE_USE> set_iter_new(Set<CM_CS_TEMPLATE_USE>* set)
{
  return Set_Iter<CM_CS_TEMPLATE_USE>{map_iter_new(&set->map)};
}

CM_CS_TEMPLATE
bool set_iter_next(Set_Iter<CM_CS_TEMPLATE_USE>* iter, K const** out_key)
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
