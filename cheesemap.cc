#pragma once

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

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

/**
 *
 * CTRL operations
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
static inline cm_group cm_group_load(const cm_u8* ctrl);
static inline cm_bitmask cm_group_match_tag(cm_group group, cm_u8 tag);
static inline cm_bitmask cm_group_match_empty_or_deleted(cm_group group);
static inline cm_bitmask cm_group_match_empty(cm_group group);
static inline cm_bitmask cm_group_match_full(cm_group group);

/**
 *
 * Scalar implementation of the group actions
 */

#if !defined(CM_IS_SIMD)
static inline cm_group cm_group_repeat(cm_u8 v)
{
    return (cm_group)v * (((cm_group)-1) / (cm_u8)~0);
}

static inline cm_group cm_group_load(const cm_u8* ctrl)
{
    assert(ctrl != NULL);

    cm_group v;
    memcpy(&v, ctrl, sizeof(v));
    return v;
}

static inline cm_bitmask cm_group_match_empty_or_deleted(cm_group group)
{
    return group & cm_group_repeat(CM_CTRL_DELETED);
}

static inline cm_bitmask cm_group_match_empty(cm_group group)
{
    return (group & (group << 1)) & cm_group_repeat(CM_CTRL_DELETED);
}

static inline cm_bitmask cm_group_match_full(cm_group group)
{
    return cm_group_match_empty_or_deleted(group) ^ cm_group_repeat(CM_CTRL_DELETED);
}

static inline cm_bitmask cm_group_match_tag(cm_group group, cm_u8 tag)
{
    cm_group cmp = group ^ cm_group_repeat(tag);
    return (cmp - cm_group_repeat(CM_CTRL_END)) & ~cmp & cm_group_repeat(CM_CTRL_DELETED);
}

#endif

/**
 *
 * Initial CTRL block for all uninited map's
 * It is used to normalize behavior between uninited map's and map's that need to grow.
 * The trick is that on insert's we don't check whether the current ctrl is nullptr,
 * instead we check whether the found ctrl is EMPTY and the map has no more space to grow
 * this means we need to resize
 */

#define CM_MAX(a, b) ((a) > (b) ? (a) : (b))

#define CM_REPEAT_1(x) x
#define CM_REPEAT_2(x) CM_REPEAT_1(x), CM_REPEAT_1(x)
#define CM_REPEAT_4(x) CM_REPEAT_2(x), CM_REPEAT_2(x)
#define CM_REPEAT_8(x) CM_REPEAT_4(x), CM_REPEAT_4(x)
#define CM_REPEAT_16(x) CM_REPEAT_8(x), CM_REPEAT_8(x)

static cm_u8 const CM_INIT_CTRL[CM_GROUP_SIZE] = {
#if CM_GROUP_SIZE == 16
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
 * Return the number of trailing zeros
 * Assume's x is NOT zero
 */
static inline cm_u32 cm_trailing_zeros(cm_usize x)
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

static inline cm_u32 cm_bitmask_trailing_zeros(cm_bitmask mask)
{
    return cm_trailing_zeros(mask) / CM_BITMASK_STRIDE;
}

/**
 *
 * Return the number of leading zeros
 * Assume's x is NOT zero
 */

static inline cm_u32 cm_leading_zeros(cm_usize x)
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

[[maybe_unused]] static inline bool cm_is_pow2(cm_usize x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

static inline cm_usize cm_next_pow2(cm_usize x)
{
    if (x <= 1)
        return 1;

    return (static_cast<cm_usize>(1) << (CM_WORD_WIDTH - cm_leading_zeros(x - 1)));
}

static inline cm_usize cm_bucket_mask_to_capacity(cm_usize bucket_mask)
{
    return ((bucket_mask + 1) / CM_LOAD_DENOM) * CM_LOAD_NUM;
}

static inline cm_usize cm_alignup(cm_usize x, cm_usize align)
{
    assert(cm_is_pow2(align) == true);
    return (x + align - 1) & ~(align - 1);
}

static inline cm_usize cm_capacity_to_bucket(cm_usize capacity)
{

    // We require 1/8 load factor, so we need 8 buckets per item
    cm_usize adjusted_capacity = capacity * CM_LOAD_DENOM / CM_LOAD_NUM;
    return CM_MAX(cm_next_pow2(adjusted_capacity), CM_GROUP_SIZE);
}

[[maybe_unused]] static inline bool cm_is_special(cm_u8 tag)
{
    // TODO: add documentation
    return (tag & CM_CTRL_DELETED) != 0;
}

static inline bool cm_is_empty(cm_u8 tag)
{
    assert(cm_is_special(tag) == true);
    return (tag & CM_CTRL_END) != 0;
}

static inline cm_usize cm_h1(cm_hash hash)
{
    // Convert the hash to the native word size used by the probing logic.
    // On narrower targets this truncates the upper bits of the hash.
    return static_cast<cm_usize>(hash);
}

static inline cm_u8 cm_h2(cm_hash hash)
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
 * Cheesemap_Bitmask_Iter iterates a Bitmask until there is nothing left
 * Pop of the next lowest bit of a bitmask
 */

typedef cm_bitmask Cheesemap_Bitmask_Iter;

static inline bool cm_bitmask_iter_next(Cheesemap_Bitmask_Iter& iter, cm_usize& out_index)
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
 * Cheesemap template macros
 * K: represents the Key type
 * V: represents the Value type
 * Hash: represents a function pointer to hash a K
 * Compare: represents a function pointer to compare 2 K's
 */

#define CM_TEMPLATE             \
    template <                  \
        typename K,             \
        typename V,             \
        Cheesemap_Hash<K> Hash, \
        Cheesemap_Compare<K> Compare>
#define CM_TEMPLATE_USE K, V, Hash, Compare

/**
 *
 * Cheesemap_Full_Iter is an iterate that iterates over full buckets
 * Returns a bucket offset if not at end of map
 */

struct Cheesemap_Full_Iter {
    Cheesemap_Bitmask_Iter bitmask_iter;
    cm_usize bucket_index;
    cm_usize num_items;
    cm_u8 const* ctrl;
};

static inline Cheesemap_Bitmask_Iter cm_full_iter_load_mask(cm_u8 const* ctrl)
{
    cm_group group = cm_group_load(ctrl);
    return cm_group_match_full(group);
}

static inline Cheesemap_Full_Iter cm_full_iter_new(cm_u8 const* ctrl, cm_usize num_items)
{
    Cheesemap_Bitmask_Iter iter = cm_full_iter_load_mask(ctrl);
    return Cheesemap_Full_Iter { iter, 0, num_items, ctrl };
}

static inline cm_usize cm_full_iter_next_inner(Cheesemap_Full_Iter& iter)
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

static inline bool cm_full_iter_next(Cheesemap_Full_Iter& iter, cm_usize& out_offset)
{
    if (iter.num_items == 0)
        return false;

    out_offset = cm_full_iter_next_inner(iter);
    iter.num_items -= 1;
    return true;
}

/**
 *
 * Iterate probe sequences with triangular numbers,
 * which is garanteed to visit every group exactly once,
 * due to the fact that our table is always a power of 2 in size
 */

struct Cheesemap_Probe_Sequence {
    cm_usize pos;
    cm_usize stride;
};

static inline void cm_probe_sequence_next(Cheesemap_Probe_Sequence& seq, cm_usize bucket_mask)
{
    assert(seq.stride <= bucket_mask);

    // TODO: add documentation

    seq.stride += CM_GROUP_SIZE;
    seq.pos += seq.stride;
    seq.pos &= bucket_mask;
}

/**
 *
 * Cheesemap_Entry represents the layout of a Key/Value pair inside the Hashmap
 */

template <typename K, typename V>
struct Cheesemap_Entry {
    K key;
    V value;
};

template <typename K, typename V>
static inline Cheesemap_Entry<K, V> cm_entry_new(K key, V value)
{
    return Cheesemap_Entry<K, V> { key, value };
}

#define CM_ENTRY_USE Cheesemap_Entry<K, V>

/**
 *
 * Cheesemap is a Swiss-Table like Hashmap
 */

CM_TEMPLATE struct Cheesemap {
    static_assert(std::is_trivial<K>::value == true, "Type K must be trivial");
    static_assert(std::is_trivial<V>::value == true, "Type V must be trivial");

    cm_usize growth_left;
    cm_usize count;
    cm_usize bucket_mask;
    cm_u8* ctrl;
};

CM_TEMPLATE
inline Cheesemap<CM_TEMPLATE_USE> cheesemap_new()
{
    return Cheesemap<CM_TEMPLATE_USE> { 0, 0, 0, const_cast<cm_u8*>(CM_INIT_CTRL) };
}

CM_TEMPLATE static inline cm_usize cheesemap_layout_for(cm_usize num_buckets, cm_usize& out_ctrl_offset)
{
    assert(cm_is_pow2(num_buckets) == true);

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
    cm_u8* entries = static_cast<cm_u8*>(aligned_alloc(alignof(CM_ENTRY_USE), total_size));
    if (entries == NULL) {
        return false;
    }

    cm_u8* ctrl = entries + ctrl_offset;
    memset(ctrl, CM_CTRL_EMPTY, num_buckets + CM_GROUP_SIZE);

    cm_usize growth_left = cm_bucket_mask_to_capacity(num_buckets - 1);
    map = Cheesemap<CM_TEMPLATE_USE> { growth_left, 0, num_buckets - 1, ctrl };
    return true;
}

CM_TEMPLATE
inline void cheesemap_drop(Cheesemap<CM_TEMPLATE_USE>& map)
{
    if (map.ctrl == CM_INIT_CTRL)
        return;

    cm_usize ctrl_offset;
    cheesemap_layout_for<CM_TEMPLATE_USE>(map.bucket_mask + 1, ctrl_offset);

    cm_u8* entries = map.ctrl - ctrl_offset;
    free(entries);
    map = cheesemap_new<CM_TEMPLATE_USE>();
}

// TODO: check whether inline improves performance
CM_TEMPLATE
static bool cheesemap_find_insert_index_in_group(const Cheesemap<CM_TEMPLATE_USE>& map, cm_group group,
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
static inline cm_u8* cheesemap_ctrl_at(const Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index)
{
    assert(index < map.bucket_mask + 1);
    return map.ctrl + index;
}

// TODO: check whether inline improves performance
CM_TEMPLATE
static cm_usize cheesemap_find_insert_index(const Cheesemap<CM_TEMPLATE_USE>& map, cm_usize h1)
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
static inline CM_ENTRY_USE* cheesemap_entry_at(const Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index)
{
    assert(map.bucket_mask != 0);
    assert(index < map.bucket_mask + 1);

    // TODO: handle zero sized entries

    auto end = reinterpret_cast<CM_ENTRY_USE*>(map.ctrl);
    return end - index - 1;
}

CM_TEMPLATE
static inline void cheesemap_set_ctrl(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index, cm_u8 tag)
{
    cm_usize index2 = ((index - CM_GROUP_SIZE) & map.bucket_mask) + CM_GROUP_SIZE;

    map.ctrl[index] = tag;
    map.ctrl[index2] = tag;
}

CM_TEMPLATE
static void cheesemap_insert_at(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize index, cm_u8 tag, const CM_ENTRY_USE& entry)
{
    cm_u8 old_ctrl = map.ctrl[index];
    map.growth_left -= static_cast<cm_usize>(cm_is_empty(old_ctrl));
    cheesemap_set_ctrl(map, index, tag);
    map.count += 1;

    auto at = cheesemap_entry_at(map, index);
    *at = entry;
}

CM_TEMPLATE static bool
cheesemap_resize(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize new_capacity)
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
static bool cheesemap_reserve(Cheesemap<CM_TEMPLATE_USE>& map, cm_usize additional)
{
    // TODO: check overflow
    cm_usize min_capacity = map.count + additional;
    cm_usize total_capacity = cm_bucket_mask_to_capacity(map.bucket_mask);
    // TODO: check for rehash if we have plenty of space left

    return cheesemap_resize(map, CM_MAX(min_capacity, total_capacity + 1));
}

CM_TEMPLATE
static bool cheesemap_find(const Cheesemap<CM_TEMPLATE_USE>& map, K key, cm_usize h1, cm_u8 h2, cm_usize& out_index)
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

// TODO: eval if inlining improves performance
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