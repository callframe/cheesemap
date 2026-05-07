#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "cheesemap requires a GNU-compatible compiler"
#endif

#if __STDC_VERSION__ < 201112L
#error "cheesemap requires C11 or later"
#endif

/* ============================================================================
 * Internal definitions
 * ========================================================================== */

/* ---- Types ---------------------------------------------------------------- */

/* Fixed-width aliases used internally by the generated map code. */
typedef uint8_t cm_u8;
typedef uint16_t cm_u16;
typedef uint32_t cm_u32;
typedef uint64_t cm_u64;

#if UINTPTR_MAX == UINT64_MAX
typedef cm_u64 cm_usize; /* Pointer-sized unsigned integer. */
#elif UINTPTR_MAX == UINT32_MAX
typedef cm_u32 cm_usize; /* Pointer-sized unsigned integer. */
#else
#error "target platform is not supported"
#endif

#if defined(__SSE2__)
#include <emmintrin.h>

/* A control group stores the metadata bytes scanned during probing. */
typedef __m128i cm_group;
typedef cm_u16 cm_bitmask;

#define CM_GROUP_SIZE 16
#define CM_NO_FALLBACK
#endif

#if !defined(CM_NO_FALLBACK)
/* Scalar fallback for platforms without SIMD */
typedef cm_usize cm_group;
typedef cm_group cm_bitmask;

#define CM_GROUP_SIZE __SIZEOF_POINTER__
#endif

typedef cm_u64 cm_hash_t;

/* ---- ctrl operations ------------------------------------------------------ */

enum {
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

/* Sentinel control bytes for an empty map. */
static const cm_u8 CM_EMPTY_CTRL[CM_GROUP_SIZE] = { [0 ... CM_GROUP_SIZE - 1] = CM_CTRL_EMPTY };

/* ---- Group operation declarations ---------------------------------------- */

static inline cm_group _cm_group_load(const cm_u8* ctrl);
static inline cm_bitmask _cm_group_match_tag(cm_group group, cm_u8 tag);
static inline cm_bitmask _cm_group_match_empty_or_deleted(cm_group group);
static inline cm_bitmask _cm_group_match_empty(cm_group group);
static inline cm_bitmask _cm_group_match_full(cm_group group);

/* ---- SSE2 backend --------------------------------------------------------- */

#if defined(__SSE2__)
static inline cm_group _cm_group_load(const cm_u8* ctrl)
{
    return _mm_loadu_si128((const cm_group*)ctrl);
}

static inline cm_bitmask _cm_group_match_tag(cm_group group, cm_u8 tag)
{
    const __m128i tagvec = _mm_set1_epi8(tag);
    __m128i cmp = _mm_cmpeq_epi8(group, tagvec);
    // movemask packs the top bit of each byte into a 16-bit mask, giving one
    // candidate bit per ctrl byte in the loaded group.
    return _mm_movemask_epi8(cmp);
}

static inline cm_bitmask _cm_group_match_empty_or_deleted(cm_group group)
{
    // EMPTY and DELETED both have their top bit set, so movemask directly gives
    // the "special ctrl byte" mask for the whole group.
    return _mm_movemask_epi8(group);
}

static inline cm_bitmask _cm_group_match_empty(cm_group group)
{
    return _cm_group_match_tag(group, CM_CTRL_EMPTY);
}

static inline cm_bitmask _cm_group_match_full(cm_group group)
{
    // FULL ctrl bytes clear the top bit, so the full-slot mask is just the
    // inverse of the special-slot mask for this 16-byte group.
    return ~_cm_group_match_empty_or_deleted(group);
}
#endif

/* ---- Scalar fallback ------------------------------------------------------ */

#if !defined(CM_NO_FALLBACK)
static inline cm_group cm_group_repeat(cm_u8 v)
{
    return (cm_group)v * (((cm_group)-1) / (cm_u8)~0);
}

static inline cm_group _cm_group_load(const cm_u8* ctrl)
{
    assert(ctrl != NULL);

    cm_group v;
    memcpy(&v, ctrl, sizeof(v));
    return v;
}

static inline cm_bitmask _cm_group_match_empty_or_deleted(cm_group group)
{
    return group & cm_group_repeat(CM_CTRL_DELETED);
}

static inline cm_bitmask _cm_group_match_empty(cm_group group)
{
    return (group & (group << 1)) & cm_group_repeat(CM_CTRL_DELETED);
}

static inline cm_bitmask _cm_group_match_full(cm_group group)
{
    return _cm_group_match_empty_or_deleted(group) ^ cm_group_repeat(CM_CTRL_DELETED);
}

static inline cm_bitmask _cm_group_match_tag(cm_group group, cm_u8 tag)
{
    cm_group cmp = group ^ cm_group_repeat(tag);
    return (cmp - cm_group_repeat(CM_CTRL_END)) & ~cmp & cm_group_repeat(CM_CTRL_DELETED);
}
#endif

/* ---- Internal Methods ----------------------------------------------------- */

#define _cm_max(a, b) ((a) > (b) ? (a) : (b))

static inline cm_hash_t _cm_h1(cm_hash_t hash)
{
    // Primary hash function for indexing into the bucket array.
    // On 32bit platforms, we ignore the top 32 bits of the hash
    return (cm_usize)hash;
}

static inline cm_u8 _cm_h2(cm_hash_t hash)
{
    cm_usize top = hash >> (sizeof(cm_hash_t) * CHAR_BIT - CM_FP_SIZE);
    return (cm_u8)(top & CM_H2_MASK);
}

static inline bool _cm_ispow2(cm_usize x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

static inline cm_usize _cm_clz(cm_usize x)
{
    assert(x != 0);
#if UINTPTR_MAX == UINT64_MAX
    return (cm_usize)__builtin_clzll(x);
#elif UINTPTR_MAX == UINT32_MAX
    return (cm_usize)__builtin_clz(x);
#else
#error "target platform is not supported"
#endif
}

static inline cm_usize _cm_bitmask_lowest_set_bit(cm_bitmask mask)
{
    assert(mask != 0);
#if CM_GROUP_SIZE == 8
    return _cm_clz(mask) / CHAR_BIT;
#elif CM_GROUP_SIZE == 16
    return _cm_clz(mask);
#else
#error "unknown group size"
#endif
}

static inline cm_usize _cm_npow2(cm_usize v)
{
    if (v <= 1)
        return 1;
    return (cm_usize)1 << (CM_WORD_WIDTH - _cm_clz(v - 1));
}

static inline cm_usize _cm_alignup(cm_usize value, cm_usize alignment)
{
    assert(_cm_ispow2(alignment));
    return (value + alignment - 1) & ~(alignment - 1);
}

static inline cm_usize _cm_capacity_to_buckets(cm_usize capacity)
{
    cm_usize min_buckets = (capacity * CM_LOAD_DENOM + CM_LOAD_NUM - 1) / CM_LOAD_NUM;
    cm_usize buckets = _cm_npow2(min_buckets);
    return _cm_max(buckets, CM_GROUP_SIZE);
}

static inline cm_usize _cm_buckets_to_capacity(cm_usize bucket_mask)
{
    cm_usize num_buckets = bucket_mask + 1;
    return (num_buckets / CM_LOAD_DENOM) * CM_LOAD_NUM;
}

static inline cm_usize _cm_ctrl_offset(cm_usize buckets, cm_usize entry_size)
{
    cm_usize offset = entry_size * buckets;
    cm_usize ctrl_align = _cm_max(entry_size, CM_GROUP_SIZE);
    return _cm_alignup(offset, ctrl_align);
}

static inline cm_u8* _cm_elem_at(cm_u8* ctrl, cm_usize index, cm_usize entry_size)
{
    return ctrl - entry_size * (index + 1);
}

static inline cm_u8* _cm_root(cm_u8* ctrl, cm_usize buckets, cm_usize entry_size)
{
    cm_usize ctrl_offset = _cm_ctrl_offset(buckets, entry_size);
    return ctrl - ctrl_offset;
}

struct _cm_sequence {
    cm_usize pos;
    cm_usize stride;
};

static inline struct _cm_sequence _cm_sequence_init(cm_usize pos, cm_usize stride)
{
    return (struct _cm_sequence) { .pos = pos, .stride = stride };
}

static inline void _cm_sequence_next(struct _cm_sequence* seq, cm_usize bucket_mask)
{
    assert(seq->stride <= bucket_mask);

    seq->stride += CM_GROUP_SIZE;
    seq->pos += seq->stride;
    seq->pos &= bucket_mask;
}

static inline bool _cm_find_insert_index_in_group(const cm_group* group, const struct _cm_sequence* seq, cm_usize bucket_mask, cm_usize* out_index)
{
    cm_bitmask mask = _cm_group_match_empty_or_deleted(*group);
    if (mask == 0)
        return false;

    cm_usize bucket_offset = _cm_bitmask_lowest_set_bit(mask);
    *out_index = (seq->pos + bucket_offset) & bucket_mask;
    return true;
}

static inline cm_usize _cm_find_insert_index(const cm_u8* ctrl, cm_usize bucket_mask, cm_hash_t hash)
{
    struct _cm_sequence seq = _cm_sequence_init(_cm_h1(hash) & bucket_mask, 0);
    while (true) {
        const cm_u8* ctrl_at = &ctrl[seq.pos];
        cm_group group = _cm_group_load(ctrl_at);

        cm_usize index;
        if (_cm_find_insert_index_in_group(&group, &seq, bucket_mask, &index))
            return index;

        _cm_sequence_next(&seq, bucket_mask);
    }
}

static inline void
_cm_ctrl_set(cm_u8* ctrl, cm_usize index, cm_u8 value)
{
    // Mirror the first CM_GROUP_SIZE ctrl bytes after the logical ctrl array so
    // group loads near the end can read a full group without wrap handling.
    cm_usize index2 = ((index - CM_GROUP_SIZE) & (index - 1)) + CM_GROUP_SIZE;
    ctrl[index] = value;
    ctrl[index2] = value;
}

static inline bool _cm_ctrl_is_empty(cm_u8 ctrl)
{
    assert((ctrl & CM_CTRL_DELETED) != 0);
    return (ctrl & CM_CTRL_END) != 0;
}

static inline void _cm_layout(cm_usize entry_size, cm_usize entry_align, cm_usize capacity, cm_usize* out_buckets, cm_usize* out_ctrl_offset, cm_usize* out_size)
{
    cm_usize buckets = _cm_capacity_to_buckets(capacity);
    cm_usize ctrl_offset = _cm_ctrl_offset(buckets, entry_size);

    cm_usize size = ctrl_offset + buckets + CM_GROUP_SIZE;
    size = _cm_alignup(size, entry_align);

    *out_buckets = buckets;
    *out_ctrl_offset = ctrl_offset;
    *out_size = size;
}

/* ---- Macro helpers -------------------------------------------------------- */

/* Internal name generation helpers. */
#define _cm_type(_name) struct _name
#define _cm_member(_name, _member) _name##_##_member

#define _cm_entry_type(_name) struct _cm_member(_name, entry)
#define _cm_iter_type(_name) struct _cm_member(_name, iter)

/* Internal type generation helpers. */

#define _cm_entry(_name, _k, _v) \
    _cm_entry_type(_name)        \
    {                            \
        _k key;                  \
        _v value;                \
    }

#define _cm_iter(_name)       \
    _cm_iter_type(_name)      \
    {                         \
        cm_bitmask curr_mask; \
        cm_usize curr_idx;    \
        cm_u8* n_ctrl;        \
        cm_u8* n_entry;       \
        cm_u8* ctrl_end;      \
    }

#define _cm_hash_fn(_name) _cm_member(_name, hash_fn)
#define _cm_hash_fn_type(_name, _k) typedef cm_hash_t (*_cm_hash_fn(_name))(_k key)

#define _cm_compare_fn(_name) _cm_member(_name, compare_fn)
#define _cm_compare_fn_type(_name, _k) typedef bool (*_cm_compare_fn(_name))(_k a, _k b)

/* ============================================================================
 * Public API
 * ========================================================================== */

/**
 * @brief Declares a strongly-typed hash map.
 *
 * Generates the entry type, callback typedefs, map struct, and function
 * declarations for a map named `_name`.
 *
 * @param[in] _name Map type name to generate.
 * @param[in] _k Key type.
 * @param[in] _v Value type.
 */
#define cheesemap(_name, _k, _v)                                                                                               \
    _cm_entry(_name, _k, _v);                                                                                                  \
    _cm_hash_fn_type(_name, _k);                                                                                               \
    _cm_compare_fn_type(_name, _k);                                                                                            \
                                                                                                                               \
    _cm_type(_name)                                                                                                            \
    {                                                                                                                          \
        cm_u8* ctrl;                                                                                                           \
        cm_usize growth_left;                                                                                                  \
        cm_usize count;                                                                                                        \
        cm_usize bucket_mask;                                                                                                  \
    };                                                                                                                         \
                                                                                                                               \
    static inline _cm_type(_name) _cm_member(_name, new)(void) { return (_cm_type(_name)) { .ctrl = (cm_u8*)CM_CTRL_EMPTY }; } \
    _cm_type(_name) _cm_member(_name, with_capacity)(cm_usize capacity);                                                       \
    bool _cm_member(_name, reserve)(_cm_type(_name) * map, cm_usize additional);                                               \
    bool _cm_member(_name, shrink_to_fit)(_cm_type(_name) * map);                                                              \
    void _cm_member(_name, drop)(_cm_type(_name) * map);                                                                       \
    bool _cm_member(_name, insert)(_cm_type(_name) * map, _k key, _v value);                                                   \
    _v* _cm_member(_name, lookup)(_cm_type(_name) * map, _k key);                                                              \
    bool _cm_member(_name, remove)(_cm_type(_name) * map, _k key, _v * out);                                                   \
                                                                                                                               \
    _cm_iter(_name);                                                                                                           \
    _cm_iter_type(_name) _cm_member(_name, iter_new)(const _cm_type(_name) * map, cm_usize start_idx);                         \
    bool _cm_member(_name, iter_next)(_cm_iter_type(_name) * iter, const _cm_type(_name) * map, _k * *out_key, _v * *out_value);

/**
 * @brief Defines the implementation of a strongly-typed hash map.
 *
 * Must be used once for each map declared with `cheesemap`.
 *
 * @param[in] _name Map type name to implement.
 * @param[in] _k Key type.
 * @param[in] _v Value type.
 * @param[in] _hash Hash function for `_k`.
 * @param[in] _compare Equality comparison function for `_k`.
 */
#define cheesemap_impl(_name, _k, _v, _hash, _compare)                                                                           \
    static void _cm_member(_name, rehash)(_cm_type(_name) * curr, cm_u8 * new_ctrl, cm_usize new_bucket_mask)                    \
    {                                                                                                                            \
        _cm_iter_type(_name) iter = _cm_member(_name, iter_new)(curr, 0);                                                        \
                                                                                                                                 \
        _k* key;                                                                                                                 \
        _v* value;                                                                                                               \
        while (_cm_member(_name, iter_next)(&iter, curr, &key, &value)) {                                                        \
            cm_hash_t hash = _hash(*key);                                                                                        \
                                                                                                                                 \
            cm_usize insert_at = _cm_find_insert_index(new_ctrl, new_bucket_mask, hash);                                         \
            _cm_ctrl_set(new_ctrl, insert_at, _cm_h2(hash));                                                                     \
                                                                                                                                 \
            _cm_entry_type(_name)* old_entry = (_cm_entry_type(_name)*)key;                                                      \
            cm_u8* new_elem = _cm_elem_at(new_ctrl, insert_at, sizeof(_cm_entry_type(_name)));                                   \
            memcpy(new_elem, old_entry, sizeof(_cm_entry_type(_name)));                                                          \
        }                                                                                                                        \
    }                                                                                                                            \
    static bool _cm_member(_name, resize)(_cm_type(_name) * map, cm_usize new_capacity)                                          \
    {                                                                                                                            \
        assert(_cm_ispow2(new_capacity) == true);                                                                                \
                                                                                                                                 \
        cm_usize buckets, ctrl_offset, size;                                                                                     \
        _cm_layout(sizeof(_cm_entry_type(_name)), _Alignof(_cm_entry_type(_name)), new_capacity, &buckets, &ctrl_offset, &size); \
        cm_usize bucket_mask = buckets - 1;                                                                                      \
                                                                                                                                 \
        cm_u8* new_ctrl = (cm_u8*)aligned_alloc(_Alignof(_cm_entry_type(_name)), size);                                          \
        if (new_ctrl == NULL)                                                                                                    \
            return false;                                                                                                        \
                                                                                                                                 \
        memset(new_ctrl, CM_CTRL_EMPTY, buckets);                                                                                \
        memcpy(new_ctrl + buckets, new_ctrl, CM_GROUP_SIZE);                                                                     \
                                                                                                                                 \
        _cm_member(_name, rehash)(map, new_ctrl, bucket_mask);                                                                   \
        if (map->ctrl != NULL && map->ctrl != (cm_u8*)CM_EMPTY_CTRL)                                                             \
            free(_cm_root(map->ctrl, buckets, sizeof(_cm_entry_type(_name))));                                                   \
                                                                                                                                 \
        map->ctrl = new_ctrl;                                                                                                    \
        map->bucket_mask = bucket_mask;                                                                                          \
        map->growth_left = _cm_buckets_to_capacity(map->bucket_mask) - map->count;                                               \
        return true;                                                                                                             \
    }                                                                                                                            \
    bool _cm_member(_name, reserve)(_cm_type(_name) * map, cm_usize additional)                                                  \
    {                                                                                                                            \
        if (map->growth_left >= additional)                                                                                      \
            return true;                                                                                                         \
        /* TODO: replace rehash */                                                                                               \
        cm_usize min_capacity = map->count + additional;                                                                         \
        cm_usize curr_capacity = _cm_buckets_to_capacity(map->bucket_mask);                                                      \
        cm_usize new_capacity = _cm_max(curr_capacity + 1, min_capacity);                                                        \
        return _cm_member(_name, resize)(map, new_capacity);                                                                     \
    }                                                                                                                            \
    _cm_type(_name) _cm_member(_name, with_capacity)(cm_usize capacity)                                                          \
    {                                                                                                                            \
        _cm_type(_name) map = _cm_member(_name, new)();                                                                          \
        if (_cm_member(_name, reserve)(&map, capacity) == false)                                                                 \
            return _cm_member(_name, new)();                                                                                     \
        return map;                                                                                                              \
    }
