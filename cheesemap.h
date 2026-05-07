#include <limits.h>
#include <stdint.h>

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
    // Number of fingerprint bytes
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

/* ---- Macro helpers -------------------------------------------------------- */

/* Internal name generation helpers. */
#define _cm_type(_name) struct _name
#define _cm_member(_name, _member) _name##_##_member

#define _cm_entry_type(_name) struct _cm_member(_name, entry)

#define _cm_entry(_name, _k, _v) \
    _cm_entry_type(_name)        \
    {                            \
        _k key;                  \
        _v value;                \
    };

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
    _cm_type(_name)                                                                                                            \
    {                                                                                                                          \
        cm_u8* ctrl;                                                                                                           \
        cm_usize growth_left;                                                                                                  \
        cm_usize count;                                                                                                        \
        cm_usize cap_mask;                                                                                                     \
    };                                                                                                                         \
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
 * @param[in] _alloc Allocation function.
 * @param[in] _dealloc Deallocation function.
 */
#define cheesemap_impl(_name, _k, _v, _hash, _compare, _alloc, _dealloc)
