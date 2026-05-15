#pragma once

#include <assert.h>
#include <limits.h>
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

/**
 *
 * Cheesemap types
 */

typedef uint8_t cm_u8;
typedef uint16_t cm_u16;
typedef uint32_t cm_u32;
typedef uint64_t cm_u64;

#if defined(__x86_64__)
typedef cm_u64 cm_usize;
#elif defined(__i386__)
typedef cm_u32 cm_usize;
#else
#error "target platform not supported"
#endif

/**
 *
 * Hash and compare operations
 */

typedef cm_u64 cm_hash;

template <typename K>
using Cheesemap_Hash = cm_hash (*)(K key);

template <typename K>
using Cheesemap_Compare = bool (*)(K key0, K key1);

using Cheesemap_Alloc = cm_u8* (*)(cm_usize size, cm_usize align);
using Cheesemap_Dealloc = void (*)(cm_u8* ptr, cm_usize size, cm_usize align);

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

enum : cm_u8 {
    // cheesemap config
    CM_LOAD_DENOM = 8,
    CM_LOAD_NUM = 7,
    //
    // ctrl ops
    // -1 as i8, all bits set, top bit = 1
    CM_CTRL_EMPTY = 0xFF, // 0b1111_1111
    // -128 as i8, top bit = 1
    CM_CTRL_DELETED = 0x80, // 0b1000_0000
    // FULL entries have top bit = 0, lower 7 bits are H2 hash
    CM_H2_MASK = 0x7F, // 0b0111_1111
    // Mask to get bottom bit
    CM_CTRL_END = 0x01, // 0b0000_0001
    // Number of fingerprint bits
    CM_FP_SIZE = 7,
    //
    // aux
    // Size of a word in bits
    CM_WORD_WIDTH = sizeof(cm_usize) * CHAR_BIT,
};

#if defined(__AVX2__)
#include <immintrin.h>

typedef __m256i cm_group;
typedef cm_u32 cm_bitmask;

#define CM_GROUP_SIZE 32
#define CM_BITMASK_STRIDE 1
#define CM_IS_SIMD

#elif defined(__SSE2__)
#include <emmintrin.h>

typedef __m128i cm_group;
typedef cm_u16 cm_bitmask;

#define CM_GROUP_SIZE 16
#define CM_BITMASK_STRIDE 1
#define CM_IS_SIMD
#endif

#if !defined(CM_IS_SIMD)

typedef cm_usize cm_group;
typedef cm_group cm_bitmask;

#define CM_GROUP_SIZE __SIZEOF_POINTER__
#define CM_BITMASK_STRIDE CHAR_BIT
#endif

/**
 *
 * Group actions interface
 */

// TODO: check whether passing my pointer is faster
inline cm_group cm_group_load(const cm_u8* ctrl);
inline cm_bitmask cm_group_match_tag(cm_group group, cm_u8 tag);
inline cm_bitmask cm_group_match_empty_or_deleted(cm_group group);
inline cm_bitmask cm_group_match_empty(cm_group group);
inline cm_bitmask cm_group_match_full(cm_group group);

/**
 *
 * AVX2 implementation of the group actions
 */

#if defined(__AVX2__)

inline cm_group cm_group_load(const cm_u8* ctrl)
{
    return _mm256_loadu_si256((const cm_group*)ctrl);
}

inline cm_bitmask cm_group_match_tag(cm_group group, cm_u8 tag)
{
    const __m256i tagvec = _mm256_set1_epi8(tag);
    __m256i cmp = _mm256_cmpeq_epi8(group, tagvec);
    // movemask packs the top bit of each byte into a 32-bit mask, giving one
    // candidate bit per ctrl byte in the loaded group.
    return _mm256_movemask_epi8(cmp);
}

inline cm_bitmask cm_group_match_empty_or_deleted(cm_group group)
{
    // EMPTY and DELETED both have their top bit set, so movemask directly gives
    // the "special ctrl byte" mask for the whole group.
    return _mm256_movemask_epi8(group);
}

inline cm_bitmask cm_group_match_empty(cm_group group)
{
    return cm_group_match_tag(group, CM_CTRL_EMPTY);
}

inline cm_bitmask cm_group_match_full(cm_group group)
{
    // FULL ctrl bytes clear the top bit, so the full-slot mask is just the
    // inverse of the special-slot mask for this 16-byte group.
    return ~cm_group_match_empty_or_deleted(group);
}

/**
 *
 * SSE2 implementation of the group actions
 */

#elif defined(__SSE2__)
inline cm_group cm_group_load(const cm_u8* ctrl)
{
    return _mm_loadu_si128((const cm_group*)ctrl);
}

inline cm_bitmask cm_group_match_tag(cm_group group, cm_u8 tag)
{
    const __m128i tagvec = _mm_set1_epi8(tag);
    __m128i cmp = _mm_cmpeq_epi8(group, tagvec);
    // movemask packs the top bit of each byte into a 16-bit mask, giving one
    // candidate bit per ctrl byte in the loaded group.
    return _mm_movemask_epi8(cmp);
}

inline cm_bitmask cm_group_match_empty_or_deleted(cm_group group)
{
    // EMPTY and DELETED both have their top bit set, so movemask directly gives
    // the "special ctrl byte" mask for the whole group.
    return _mm_movemask_epi8(group);
}

inline cm_bitmask cm_group_match_empty(cm_group group)
{
    return cm_group_match_tag(group, CM_CTRL_EMPTY);
}

inline cm_bitmask cm_group_match_full(cm_group group)
{
    // FULL ctrl bytes clear the top bit, so the full-slot mask is just the
    // inverse of the special-slot mask for this 16-byte group.
    return ~cm_group_match_empty_or_deleted(group);
}
#endif

/**
 *
 * Scalar implementation of the group actions
 */

#if !defined(CM_IS_SIMD)
inline cm_group cm_group_repeat(cm_u8 v)
{
    return (cm_group)v * (((cm_group)-1) / (cm_u8)~0);
}

inline cm_group cm_group_load(const cm_u8* ctrl)
{
    assert(ctrl != NULL);

    cm_group v;
    memcpy(&v, ctrl, sizeof(v));
    return v;
}

inline cm_bitmask cm_group_match_empty_or_deleted(cm_group group)
{
    return group & cm_group_repeat(CM_CTRL_DELETED);
}

inline cm_bitmask cm_group_match_empty(cm_group group)
{
    return (group & (group << 1)) & cm_group_repeat(CM_CTRL_DELETED);
}

inline cm_bitmask cm_group_match_full(cm_group group)
{
    return cm_group_match_empty_or_deleted(group) ^ cm_group_repeat(CM_CTRL_DELETED);
}

inline cm_bitmask cm_group_match_tag(cm_group group, cm_u8 tag)
{
    cm_group cmp = group ^ cm_group_repeat(tag);
    return (cmp - cm_group_repeat(CM_CTRL_END)) & ~cmp & cm_group_repeat(CM_CTRL_DELETED);
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

#define CM_MAX(a, b) ((a) > (b) ? (a) : (b))

#define CM_REPEAT_1(x) x
#define CM_REPEAT_2(x) CM_REPEAT_1(x), CM_REPEAT_1(x)
#define CM_REPEAT_4(x) CM_REPEAT_2(x), CM_REPEAT_2(x)
#define CM_REPEAT_8(x) CM_REPEAT_4(x), CM_REPEAT_4(x)
#define CM_REPEAT_16(x) CM_REPEAT_8(x), CM_REPEAT_8(x)
#define CM_REPEAT_32(x) CM_REPEAT_16(x), CM_REPEAT_16(x)

inline constexpr cm_u8 CM_INIT_CTRL[CM_GROUP_SIZE] = {
#if CM_GROUP_SIZE == 32
    CM_REPEAT_32(CM_CTRL_EMPTY)
#elif CM_GROUP_SIZE == 16
    CM_REPEAT_16(CM_CTRL_EMPTY)
#elif CM_GROUP_SIZE == 8
    CM_REPEAT_8(CM_CTRL_EMPTY)
#elif CM_GROUP_SIZE == 4
    CM_REPEAT_4(CM_CTRL_EMPTY)
#else
#error "group size is not supported"
#endif
};

/**
 *
 * Return the number of trailing zero bits.
 * Returns CM_WORD_WIDTH when x is zero.
 */
inline cm_u32 cm_trailing_zeros(cm_usize x)
{
    // TODO: this branch fixes a problem that shouldn't exist
    if (x == 0) {
        return CM_WORD_WIDTH;
    }

#if defined(__x86_64__)
    return __builtin_ctzll(x);
#elif defined(__i386__)
    return __builtin_ctz(x);
#else
#error "target platform is not supported"
#endif
}

inline cm_u32 cm_bitmask_trailing_zeros(cm_bitmask mask)
{
    return cm_trailing_zeros(mask) / CM_BITMASK_STRIDE;
}

/**
 *
 * Return the number of leading zero bits.
 * Returns CM_WORD_WIDTH when x is zero.
 */

inline cm_u32 cm_leading_zeros(cm_usize x)
{
    // TODO: this branch fixes a problem that shouldn't exist
    if (x == 0) {
        return CM_WORD_WIDTH;
    }

#if defined(__x86_64__)
    return __builtin_clzll(x);
#elif defined(__i386__)
    return __builtin_clz(x);
#else
#error "target platform is not supported"
#endif
}

[[maybe_unused]] inline bool cm_is_pow2(cm_usize x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

inline cm_usize cm_next_pow2(cm_usize x)
{
    if (x <= 1)
        return 1;

    return (static_cast<cm_usize>(1) << (CM_WORD_WIDTH - cm_leading_zeros(x - 1)));
}

inline cm_usize cm_bucket_mask_to_capacity(cm_usize bucket_mask)
{
    // Capacity is the maximum number of full buckets allowed before growth.
    // Cheesemap keeps at least 1/8 of the buckets empty, so capacity is 7/8
    // of the bucket count.
    return ((bucket_mask + 1) / CM_LOAD_DENOM) * CM_LOAD_NUM;
}

inline cm_usize cm_alignup(cm_usize x, cm_usize align)
{
    assert(cm_is_pow2(align) == true);
    return (x + align - 1) & ~(align - 1);
}

[[maybe_unused]] bool cm_isaligned(cm_usize x, cm_usize align)
{
    assert(cm_is_pow2(align) == true);
    return (x & (align - 1)) == 0;
}

inline cm_usize cm_capacity_to_bucket(cm_usize capacity)
{

    // Choose enough buckets to hold `capacity` items at a 7/8 max load factor.
    cm_usize adjusted_capacity = capacity * CM_LOAD_DENOM / CM_LOAD_NUM;
    return CM_MAX(cm_next_pow2(adjusted_capacity), CM_GROUP_SIZE);
}

[[maybe_unused]] inline bool cm_is_special(cm_u8 tag)
{
    // Returns true for special control bytes, which have their high bit set.
    // EMPTY and DELETED are special; FULL control bytes are not.
    return (tag & CM_CTRL_DELETED) != 0;
}

inline bool cm_is_empty(cm_u8 tag)
{
    assert(cm_is_special(tag) == true);
    return (tag & CM_CTRL_END) != 0;
}

inline cm_usize cm_h1(cm_hash hash)
{
    // Convert the hash to the native word size used by the probing logic.
    // On narrower targets this truncates the upper bits of the hash.
    return static_cast<cm_usize>(hash);
}

inline cm_u8 cm_h2(cm_hash hash)
{

    // On 64-bit platforms this leaves exactly 7 bits after the shift.
    // On 32-bit platforms cm_usize is 32-bit while cm_hash is 64-bit, so
    // shifting by 25 leaves a 39-bit intermediate value instead.
    cm_usize shifted = hash >> (sizeof(cm_usize) * CHAR_BIT - CM_FP_SIZE);

    // Mask the intermediate value down to the 7 fingerprint bits stored in
    // the ctrl block.
    return static_cast<cm_u8>(shifted & CM_H2_MASK);
}

/**
 *
 * Cheesemap_Bitmask_Iter walks the set bits in a bitmask.
 * Each step returns the lowest set bit and clears it from the iterator.
 */

typedef cm_bitmask Cheesemap_Bitmask_Iter;

inline bool cm_bitmask_iter_next(Cheesemap_Bitmask_Iter& iter, cm_usize& out_index)
{
    if (iter == 0)
        return false;

    cm_usize bit = cm_bitmask_trailing_zeros(iter);
    iter &= (iter - 1);
    out_index = bit;

    return true;
}

/**
 *
 * Cheesemap_Full_Iter walks buckets whose control bytes are FULL.
 * Each step returns the bucket index for one occupied entry.
 */

struct Cheesemap_Full_Iter {
    Cheesemap_Bitmask_Iter bitmask_iter;
    cm_usize bucket_index;
    cm_usize num_items;
    cm_u8 const* ctrl;
};

inline Cheesemap_Bitmask_Iter cm_full_iter_load_mask(cm_u8 const* ctrl)
{
    cm_group group = cm_group_load(ctrl);
    return cm_group_match_full(group);
}

inline Cheesemap_Full_Iter cm_full_iter_new(cm_u8 const* ctrl, cm_usize num_items)
{
    Cheesemap_Bitmask_Iter iter = cm_full_iter_load_mask(ctrl);
    return Cheesemap_Full_Iter { iter, 0, num_items, ctrl };
}

inline cm_usize cm_full_iter_next_inner(Cheesemap_Full_Iter& iter)
{
    while (true) {
        cm_usize group_offset;
        if (cm_bitmask_iter_next(iter.bitmask_iter, group_offset)) {
            return iter.bucket_index + group_offset;
        }

        iter.ctrl += CM_GROUP_SIZE;
        iter.bitmask_iter = cm_full_iter_load_mask(iter.ctrl);
        iter.bucket_index += CM_GROUP_SIZE;
    }
}

inline bool cm_full_iter_next(Cheesemap_Full_Iter& iter, cm_usize& out_offset)
{
    if (iter.num_items == 0)
        return false;

    out_offset = cm_full_iter_next_inner(iter);
    iter.num_items -= 1;
    return true;
}

/**
 *
 * Probe sequences advance by triangular numbers over control groups.
 * Because the table size is always a power of two, this visits every group
 * before repeating.
 */

struct Cheesemap_Probe_Sequence {
    cm_usize pos;
    cm_usize stride;
};

inline void cm_probe_sequence_next(Cheesemap_Probe_Sequence& seq, cm_usize bucket_mask)
{
    assert(seq.stride <= bucket_mask);

    // Advance by one more group than the previous step. This forms a triangular
    // probe sequence over groups:
    //
    //   step:     0   1   2   3   4
    //   stride:   0   1   2   3   4 groups
    //   offset:   0   1   3   6  10 groups from start
    //
    // Because the table has a power-of-two number of buckets, masking by
    // `bucket_mask` wraps this sequence through every group.

    seq.stride += CM_GROUP_SIZE;
    seq.pos += seq.stride;
    seq.pos &= bucket_mask;
}

/**
 *
 * Cheesemap_Entry stores one key/value pair in the table's entry array.
 */

template <typename K, typename V>
struct Cheesemap_Entry {
    K key;
    V value;
};

template <typename K, typename V>
Cheesemap_Entry<K, V> cm_entry_new(K key, V value)
{
    return Cheesemap_Entry<K, V> { key, value };
}

#define CM_ENTRY_USE Cheesemap_Entry<K, V>

/**
 *
 * Template parameter macros used to keep the public and internal function
 * signatures short.
 *
 * K: key type
 * V: value type
 * Hash: function pointer that hashes a key
 * Compare: function pointer that compares two keys for equality
 * Alloc: function pointer that allocates memory for the map
 * Dealloc: function pointer that deallocates memory for the map
 */

#define CM_TEMPLATE                   \
    template <                        \
        typename K,                   \
        typename V,                   \
        Cheesemap_Hash<K> Hash,       \
        Cheesemap_Compare<K> Compare, \
        Cheesemap_Alloc Alloc,        \
        Cheesemap_Dealloc Dealloc>
#define CM_TEMPLATE_USE K, V, Hash, Compare, Alloc, Dealloc

/**
 *
 * Cheesemap is a Swiss-table-style hash map.
 *
 * The map stores keys and values in a contiguous entry array and keeps probing
 * metadata in a separate control-byte array. `growth_left` tracks how many
 * more empty buckets may be filled before the table must grow.
 */

CM_TEMPLATE struct Cheesemap {
    cm_usize growth_left;
    cm_usize count;
    cm_usize bucket_mask;
    cm_u8* ctrl;
};

CM_TEMPLATE
Cheesemap<CM_TEMPLATE_USE> cheesemap_new()
{
    return Cheesemap<CM_TEMPLATE_USE> { 0, 0, 0, const_cast<cm_u8*>(CM_INIT_CTRL) };
}

CM_TEMPLATE
cm_usize cheesemap_layout_for(cm_usize num_buckets, cm_usize& out_ctrl_offset)
{
    assert(cm_is_pow2(num_buckets) == true);

    // Allocate entries and control bytes in one block:
    //
    //   [entries, stored in reverse bucket order] [padding] [ctrl bytes] [ctrl clone]
    //
    // `ctrl` points at the first control byte. Entries are addressed backwards from
    // `ctrl`, so bucket 0 lives immediately before the control region and bucket
    // N - 1 lives at the start of the allocation. The extra CM_GROUP_SIZE control
    // bytes clone the first group so group loads can wrap without a branch.

    cm_usize ctrl_align = CM_MAX(CM_GROUP_SIZE, alignof(CM_ENTRY_USE));

    // TODO: check for overflow

    cm_usize base_offset = sizeof(CM_ENTRY_USE) * num_buckets;
    cm_usize ctrl_offset = cm_alignup(base_offset, ctrl_align);

    cm_usize total_size = ctrl_offset + num_buckets + CM_GROUP_SIZE;
    total_size = cm_alignup(total_size, alignof(CM_ENTRY_USE));

    out_ctrl_offset = ctrl_offset;
    return total_size;
}

CM_TEMPLATE
bool cheesemap_new_with(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize init_capacity)
{
    cm_usize num_buckets = cm_capacity_to_bucket(init_capacity);

    cm_usize ctrl_offset;
    cm_usize total_size = cheesemap_layout_for<CM_TEMPLATE_USE>(num_buckets, ctrl_offset);

    assert(total_size % alignof(CM_ENTRY_USE) == 0);

    cm_u8* entries = Alloc(total_size, alignof(CM_ENTRY_USE));
    if (entries == NULL) {
        return false;
    }
    assert(cm_isaligned((cm_usize)entries, alignof(CM_ENTRY_USE)) == true);

    cm_u8* ctrl = entries + ctrl_offset;
    memset(ctrl, CM_CTRL_EMPTY, num_buckets + CM_GROUP_SIZE);

    cm_usize growth_left = cm_bucket_mask_to_capacity(num_buckets - 1);
    map = Cheesemap<CM_TEMPLATE_USE> { growth_left, 0, num_buckets - 1, ctrl };
    return true;
}

CM_TEMPLATE
void cheesemap_drop(Cheesemap<CM_TEMPLATE_USE>& map)
{
    if (map.ctrl == CM_INIT_CTRL)
        return;

    cm_usize ctrl_offset;
    cm_usize total_size = cheesemap_layout_for<CM_TEMPLATE_USE>(map.bucket_mask + 1, ctrl_offset);

    cm_u8* entries = map.ctrl - ctrl_offset;
    Dealloc(entries, total_size, alignof(CM_ENTRY_USE));
    map = cheesemap_new<CM_TEMPLATE_USE>();
}

CM_TEMPLATE
bool cheesemap_find_insert_index_in_group(const Cheesemap<CM_TEMPLATE_USE>& map, cm_group group,
    const Cheesemap_Probe_Sequence& seq, cm_usize& offset)
{
    cm_bitmask mask = cm_group_match_empty_or_deleted(group);
    if (mask == 0)
        return false;

    cm_usize lowest = cm_bitmask_trailing_zeros(mask);
    offset = (seq.pos + lowest) & map.bucket_mask;
    return true;
}

CM_TEMPLATE
cm_u8* cheesemap_ctrl_at(const Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index)
{
    assert(index < map.bucket_mask + 1);
    return map.ctrl + index;
}

CM_TEMPLATE
cm_usize cheesemap_find_insert_index(const Cheesemap<CM_TEMPLATE_USE>& map, cm_usize h1)
{
    auto seq = Cheesemap_Probe_Sequence {
        h1 & map.bucket_mask,
        0,
    };

    while (true) {
        cm_u8* ctrl_at = cheesemap_ctrl_at(map, seq.pos);
        cm_group group = cm_group_load(ctrl_at);

        cm_usize offset;
        if (cheesemap_find_insert_index_in_group(map, group, seq, offset)) {
            return offset;
        }

        cm_probe_sequence_next(seq, map.bucket_mask);
    }
}

CM_TEMPLATE
CM_ENTRY_USE* cheesemap_entry_at(const Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index)
{
    assert(map.bucket_mask != 0);
    assert(index < map.bucket_mask + 1);

    auto end = reinterpret_cast<CM_ENTRY_USE*>(map.ctrl);
    return end - index - 1;
}

CM_TEMPLATE
void cheesemap_set_ctrl(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index, cm_u8 tag)
{
    cm_usize index2 = ((index - CM_GROUP_SIZE) & map.bucket_mask) + CM_GROUP_SIZE;

    map.ctrl[index] = tag;
    map.ctrl[index2] = tag;
}

CM_TEMPLATE
void cheesemap_insert_at(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index, cm_u8 tag, const CM_ENTRY_USE& entry)
{
    cm_u8 old_ctrl = map.ctrl[index];
    map.growth_left -= static_cast<cm_usize>(cm_is_empty(old_ctrl));
    cheesemap_set_ctrl(map, index, tag);
    map.count += 1;

    auto at = cheesemap_entry_at(map, index);
    *at = entry;
}

CM_TEMPLATE
bool cheesemap_resize(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize new_capacity)
{
    Cheesemap<CM_TEMPLATE_USE> new_map;
    if (!cheesemap_new_with(new_map, new_capacity)) {
        return false;
    }

    Cheesemap_Full_Iter iter = cm_full_iter_new(map.ctrl, map.count);
    cm_usize ctrl_offset;

    while (cm_full_iter_next(iter, ctrl_offset)) {
        CM_ENTRY_USE* src = cheesemap_entry_at(map, ctrl_offset);
        cm_hash hash = Hash(src->key);

        cm_usize insert_at = cheesemap_find_insert_index(new_map, cm_h1(hash));
        cheesemap_set_ctrl(new_map, insert_at, cm_h2(hash));

        CM_ENTRY_USE* dest = cheesemap_entry_at(new_map, insert_at);
        memcpy(dest, src, sizeof(CM_ENTRY_USE));
    }

    new_map.count = map.count;
    new_map.growth_left -= map.count;

    cheesemap_drop(map);
    map = new_map;
    return true;
}

CM_TEMPLATE
bool cheesemap_reserve(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize additional)
{
    // TODO: check overflow
    cm_usize min_capacity = map.count + additional;
    cm_usize total_capacity = cm_bucket_mask_to_capacity(map.bucket_mask);
    // TODO: check for rehash if we have plenty of space left

    if (min_capacity <= total_capacity) {
        return true;
    }

    return cheesemap_resize(map, CM_MAX(min_capacity, total_capacity + 1));
}

CM_TEMPLATE
bool cheesemap_find(const Cheesemap<CM_TEMPLATE_USE>& map, K key, cm_usize h1, cm_u8 h2, cm_usize& out_index)
{
    auto seq = Cheesemap_Probe_Sequence {
        h1 & map.bucket_mask,
        0,
    };

    while (true) {
        cm_u8* ctrl_at = cheesemap_ctrl_at(map, seq.pos);
        cm_group group = cm_group_load(ctrl_at);

        cm_bitmask match_mask = cm_group_match_tag(group, h2);
        while (match_mask != 0) {
            cm_usize bit = cm_bitmask_trailing_zeros(match_mask);
            cm_usize index = (seq.pos + bit) & map.bucket_mask;

            auto entry = cheesemap_entry_at(map, index);
            if (Compare(key, entry->key)) {
                out_index = index;
                return true;
            }

            match_mask &= (match_mask - 1);
        }

        if (cm_group_match_empty(group) != 0) {
            return false;
        }

        cm_probe_sequence_next(seq, map.bucket_mask);
    }
}

CM_TEMPLATE
bool cheesemap_lookup(const Cheesemap<CM_TEMPLATE_USE>& map, K key, V& out_value)
{
    cm_hash hash = Hash(key);
    cm_usize h1 = cm_h1(hash);
    cm_u8 h2 = cm_h2(hash);

    cm_usize index;
    if (cheesemap_find(map, key, h1, h2, index)) {
        auto entry = cheesemap_entry_at(map, index);
        out_value = entry->value;
        return true;
    }

    return false;
}

CM_TEMPLATE
bool cheesemap_insert(Cheesemap<CM_TEMPLATE_USE>& map, K key, V value)
{

    cm_hash hash = Hash(key);
    cm_usize h1 = cm_h1(hash);
    cm_u8 h2 = cm_h2(hash);

    cm_usize insert_at;
    if (cheesemap_find(map, key, h1, h2, insert_at)) {
        auto entry = cheesemap_entry_at(map, insert_at);
        entry->value = value;
        return true;
    }

    insert_at = cheesemap_find_insert_index(map, h1);
    if (map.growth_left == 0 && cm_is_empty(map.ctrl[insert_at])) {
        if (!cheesemap_reserve(map, 1)) {
            return false;
        }

        insert_at = cheesemap_find_insert_index(map, h1);
    }

    cheesemap_insert_at(map, insert_at, h2, cm_entry_new(key, value));
    return true;
}

CM_TEMPLATE
bool cheesemap_remove(Cheesemap<CM_TEMPLATE_USE>& map, K key)
{
    cm_hash hash = Hash(key);
    cm_usize index;
    if (!cheesemap_find(map, key, cm_h1(hash), cm_h2(hash), index)) {
        return false;
    }
    cm_usize index_before = (index - CM_GROUP_SIZE) & map.bucket_mask;

    // We can't just mark the slot EMPTY: find() stops probing at EMPTY, so if this slot
    // was part of a full probe chain, lookups for keys displaced past it would wrongly
    // terminate here. So we check the bytes around `index`: if there's an EMPTY nearby
    // (within one group), find() was going to stop there anyway and we can safely mark
    // this slot EMPTY too. Otherwise we must mark it DELETED so probing continues past it.
    cm_group group_before = cm_group_load(cheesemap_ctrl_at(map, index_before));
    cm_group group_after = cm_group_load(cheesemap_ctrl_at(map, index));
    cm_bitmask empty_before = cm_group_match_empty(group_before);
    cm_bitmask empty_after = cm_group_match_empty(group_after);

    cm_usize num_zeros = cm_leading_zeros(empty_before) + cm_trailing_zeros(empty_after);
    if (num_zeros >= CM_GROUP_SIZE) {
        cheesemap_set_ctrl(map, index, CM_CTRL_DELETED);
    } else {
        cheesemap_set_ctrl(map, index, CM_CTRL_EMPTY);
        map.growth_left += 1;
    }
    map.count -= 1;
    return true;
}

/**
 *
 * Iterator for occupied Cheesemap entries.
 *
 * Iteration follows bucket order, skipping EMPTY and DELETED slots. Each call
 * to cm_iter_next returns pointers to the stored key and value. The key pointer
 * is const because changing a key in place would break the table's hash
 * invariant.
 */

CM_TEMPLATE
struct Cheesemap_Iter {
    Cheesemap_Full_Iter full_iter;
    Cheesemap<CM_TEMPLATE_USE>& map;
};

CM_TEMPLATE
Cheesemap_Iter<CM_TEMPLATE_USE> cm_iter_new(Cheesemap<CM_TEMPLATE_USE>& map)
{
    return Cheesemap_Iter { cm_full_iter_new(map.ctrl, map.count), map };
}

CM_TEMPLATE
bool cm_iter_next(Cheesemap_Iter<CM_TEMPLATE_USE>& iter, K const*& out_key, V*& out_value)
{
    cm_usize offset;

    if (!cm_full_iter_next(iter.full_iter, offset)) {
        return false;
    }

    auto entry = cheesemap_entry_at(iter.map, offset);
    out_key = &entry->key;
    out_value = &entry->value;

    return true;
}

/**
 *
 * Template parameter macros used to keep the public Cheeseset function
 * signatures short.
 *
 * K: key type
 * Hash: function pointer that hashes a key
 * Compare: function pointer that compares two keys for equality
 * Alloc: function pointer that allocates memory for the set
 * Dealloc: function pointer that deallocates memory for the set
 */

#define CM_CS_TEMPLATE                \
    template <                        \
        typename K,                   \
        Cheesemap_Hash<K> Hash,       \
        Cheesemap_Compare<K> Compare, \
        Cheesemap_Alloc Alloc,        \
        Cheesemap_Dealloc Dealloc>
#define CM_CS_TEMPLATE_USE K, Hash, Compare, Alloc, Dealloc

struct Cheeseset_Unit { };
static_assert(sizeof(Cheeseset_Unit) == 1, "Cheeseset_Unit must be exactly one byte");

#define CM_CS_INNER_TEMPLATE_USE K, Cheeseset_Unit, Hash, Compare, Alloc, Dealloc

/**
 *
 * Cheeseset is a Swiss-table-style hash set backed by Cheesemap.
 *
 * The set stores keys in the backing map and uses Cheeseset_Unit as the value.
 * This gives Cheeseset the same probing, allocation, resizing, and removal
 * behavior as Cheesemap while exposing only membership operations.
 */

CM_CS_TEMPLATE
struct Cheeseset {
    Cheesemap<CM_CS_INNER_TEMPLATE_USE> map;
};

CM_CS_TEMPLATE
Cheeseset<CM_CS_TEMPLATE_USE> cheeseset_new()
{
    return Cheeseset<CM_CS_TEMPLATE_USE> { cheesemap_new<CM_CS_INNER_TEMPLATE_USE>() };
}

CM_CS_TEMPLATE
bool cheeseset_new_with(Cheeseset<CM_CS_TEMPLATE_USE>& set, cm_usize init_capacity)
{
    return cheesemap_new_with(set.map, init_capacity);
}

CM_CS_TEMPLATE
void cheeseset_drop(Cheeseset<CM_CS_TEMPLATE_USE>& set)
{
    cheesemap_drop(set.map);
}

CM_CS_TEMPLATE
bool cheeseset_insert(Cheeseset<CM_CS_TEMPLATE_USE>& set, K key)
{
    return cheesemap_insert(set.map, key, Cheeseset_Unit { });
}

CM_CS_TEMPLATE
bool cheeseset_lookup(const Cheeseset<CM_CS_TEMPLATE_USE>& set, K key)
{
    Cheeseset_Unit unit;
    return cheesemap_lookup(set.map, key, unit);
}

CM_CS_TEMPLATE
bool cheeseset_remove(Cheeseset<CM_CS_TEMPLATE_USE>& set, K key)
{
    return cheesemap_remove(set.map, key);
}

/**
 *
 * Iterator for occupied Cheeseset entries.
 *
 * Iteration follows bucket order, skipping EMPTY and DELETED slots. Each call
 * to cheeseset_iter_next returns a pointer to the stored key. The key pointer
 * is const because changing a key in place would break the table's hash
 * invariant.
 */

CM_CS_TEMPLATE
struct Cheeseset_Iter {
    Cheesemap_Iter<CM_CS_INNER_TEMPLATE_USE> map_iter;
};

CM_CS_TEMPLATE
Cheeseset_Iter<CM_CS_TEMPLATE_USE> cheeseset_iter_new(Cheeseset<CM_CS_TEMPLATE_USE>& set)
{
    return Cheeseset_Iter { cm_iter_new(set.map) };
}

CM_CS_TEMPLATE
bool cheeseset_iter_next(Cheeseset_Iter<CM_CS_TEMPLATE_USE>& iter, K const*& out_key)
{
    K const* key;
    Cheeseset_Unit* value;

    if (!cm_iter_next(iter.map_iter, key, value)) {
        return false;
    }

    out_key = key;
    return true;
}
