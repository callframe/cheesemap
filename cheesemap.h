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
typedef __m128i cm_group_t;
typedef cm_u16 cm_bitmask_t;

#define CM_GROUP_SIZE 16
#define CM_NO_FALLBACK
#endif

#if !defined(CM_NO_FALLBACK)
/* Scalar fallback for platforms without SIMD */
typedef cm_usize cm_group_t;
typedef cm_group_t cm_bitmask_t;

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
extern const cm_u8 CM_EMPTY_CTRL[CM_GROUP_SIZE];

cm_group_t _cm_group_load(const cm_u8* ctrl);
cm_bitmask_t _cm_group_match_tag(cm_group_t group, cm_u8 tag);
cm_bitmask_t _cm_group_match_empty_or_deleted(cm_group_t group);
cm_bitmask_t _cm_group_match_empty(cm_group_t group);
cm_bitmask_t _cm_group_match_full(cm_group_t group);

/* ---- Internal Methods ----------------------------------------------------- */

#define _cm_max(a, b) ((a) > (b) ? (a) : (b))

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

void _cm_layout(cm_usize entry_size, cm_usize entry_align, cm_usize capacity, cm_usize* out_buckets, cm_usize* out_ctrl_offset, cm_usize* out_size);
cm_usize _cm_buckets_to_capacity(cm_usize bucket_mask);

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

#define _cm_iter(_name)         \
    _cm_iter_type(_name)        \
    {                           \
        cm_bitmask_t curr_mask; \
        cm_usize curr_idx;      \
        cm_u8* n_ctrl;          \
        cm_u8* n_entry;         \
        cm_u8* ctrl_end;        \
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
    _cm_iter(_name);                                                                                                           \
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
    bool _cm_member(_name, remove)(_cm_type(_name) * map, _k key, _v * out);

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
    static void _cm_member(_name, rehash)(cm_u8 * old_ctrl, cm_u8 * new_ctrl)                                                    \
    {                                                                                                                            \
    }                                                                                                                            \
    static bool _cm_member(_name, resize)(_cm_type(_name) * map, cm_usize new_capacity)                                          \
    {                                                                                                                            \
        assert(_cm_ispow2(new_capacity) == true);                                                                                \
                                                                                                                                 \
        cm_usize buckets, ctrl_offset, size;                                                                                     \
        _cm_layout(sizeof(_cm_entry_type(_name)), _Alignof(_cm_entry_type(_name)), new_capacity, &buckets, &ctrl_offset, &size); \
                                                                                                                                 \
        cm_u8* new_ctrl = (cm_u8*)aligned_alloc(_Alignof(_cm_entry_type(_name)), size);                                          \
        if (new_ctrl == NULL)                                                                                                    \
            return false;                                                                                                        \
                                                                                                                                 \
        memset(new_ctrl, CM_CTRL_EMPTY, buckets);                                                                                \
        memcpy(new_ctrl + buckets, new_ctrl, CM_GROUP_SIZE);                                                                     \
                                                                                                                                 \
        _cm_member(_name, rehash)(map->ctrl, new_ctrl);                                                                          \
        free(map->ctrl);                                                                                                         \
                                                                                                                                 \
        map->ctrl = new_ctrl;                                                                                                    \
        map->bucket_mask = buckets - 1;                                                                                          \
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
