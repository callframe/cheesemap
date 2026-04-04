#ifndef CHEESEMAP_
#define CHEESEMAP_

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////
// options and includes
//

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t cm_u8;
typedef uint16_t cm_u16;
typedef uint32_t cm_u32;
typedef uint64_t cm_u64;

#if UINTPTR_MAX == UINT64_MAX
typedef cm_u64 cm_usize;
#elif UINTPTR_MAX == UINT32_MAX
typedef cm_u32 cm_usize;
#else
#error "unsupported uintptr_t width"
#endif

void CM_PANIC_SYM(const char* file, cm_u32 line, const char* fmt, ...);

#ifdef NDEBUG
#define cm_assert(cond)
#else
#define cm_assert(cond)                                                   \
  do {                                                                    \
    if (!(cond))                                                          \
      CM_PANIC_SYM(__FILE__, __LINE__, "cm_assertion failed: %s", #cond); \
  } while (0)
#endif

#ifdef CM_ENABLE_SSE2
#include <emmintrin.h>

typedef __m128i group_t;
typedef cm_u16 bitmask_t;
#define CM_GROUP_SIZE 16
#define CM_NO_FALLBACK
#endif

#ifndef CM_NO_FALLBACK
typedef cm_usize group_t;
typedef group_t bitmask_t;
#define CM_GROUP_SIZE __SIZEOF_POINTER__
#endif

////////////////////////////////
// cheesemap callback functions
//

typedef cm_u64 cm_hash_t;

/* hash and compare methods */
typedef cm_hash_t (*cm_hash_fn)(const cm_u8* key, cm_u8* user);
typedef bool (*cm_compare_fn)(const cm_u8* key1, const cm_u8* key2,
                              cm_u8* user);

/* allocator methods */
typedef cm_u8* (*cm_alloc_fn)(cm_usize size, cm_u8* user);
typedef void (*cm_dealloc_fn)(cm_u8* ptr, cm_u8* user);

////////////////////////////////
// raw cheesemap implementation
//
// layout:
// [entries...][control bits...][mirror first CM_GROUP_SIZE bits]

enum {
  // cheesemap config
  CM_INITIAL_CAPACITY = CM_GROUP_SIZE,
  CM_LOAD_DENOM = 8,
  CM_LOAD_NUM = 7,
  //
  // ctrl ops
  // -1 as i8, all bits set, top bit = 1
  CM_CTRL_EMPTY = 0xFF,  // 0b1111_1111
  // -128 as i8, top bit = 1
  CM_CTRL_DELETED = 0x80,  // 0b1000_0000
  // FULL entries have top bit = 0, lower 7 bits are H2 hash
  CM_H2_MASK = 0x7F,  // 0b0111_1111
  // Mask to get bottom bit
  CM_CTRL_END = 0x01,  // 0b0000_0001
  // Number of fingerprint bytes
  CM_FP_SIZE = 7,
  //
  // aux
  // Size of a word in bits
  CM_WORD_WIDTH = sizeof(cm_usize) * CHAR_BIT,

};

extern const cm_u8 CM_CTRL_STATIC_EMPTY[CM_GROUP_SIZE];

struct cm_type {
  cm_usize key_size;
  cm_usize value_size;
  cm_usize value_offset;
  cm_usize entry_size;
};

#define cm_type_construct(key_size, value_size, value_offset, entry_size) \
  ((struct cm_type){key_size, value_size, value_offset, entry_size})

struct cheesemap_raw {
  // number of buckets as mask
  cm_usize bucket_mask;
  // number of entries in the map
  cm_usize count;
  // number of entry left until resize
  cm_usize growth_left;
  // pointer to the control bytes
  cm_u8* ctrl;
};

#define cm_raw_new() \
  ((struct cheesemap_raw){.ctrl = (cm_u8*)CM_CTRL_STATIC_EMPTY})

bool cm_raw_new_with(struct cheesemap_raw* map, cm_alloc_fn alloc,
                     cm_u8* user, const struct cm_type* type,
                     cm_usize initial_capacity);
void cm_raw_drop(struct cheesemap_raw* map, cm_dealloc_fn dealloc,
                 cm_u8* user, const struct cm_type* type);
bool cm_raw_reserve(struct cheesemap_raw* map, cm_hash_fn hash,
                    cm_alloc_fn alloc, cm_dealloc_fn dealloc, cm_u8* user,
                    const struct cm_type* type, cm_usize additional);
bool cm_raw_lookup(const struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, cm_u8* user,
                   const struct cm_type* type, const cm_u8* key,
                   cm_u8** out_value);
bool cm_raw_remove(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, cm_u8* user,
                   const struct cm_type* type, const cm_u8* key,
                   cm_u8* out_value);
bool cm_raw_insert(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_alloc_fn alloc, cm_dealloc_fn dealloc, cm_u8* user,
                   const struct cm_type* type, const cm_u8* key,
                   const cm_u8* value);

////////////////////////////////
// cheesemap implementation
//

struct cheesemap {
  struct cm_type type;
  cm_u8* user;
  cm_hash_fn hash;
  cm_compare_fn compare;
  cm_alloc_fn alloc;
  cm_dealloc_fn dealloc;
  struct cheesemap_raw raw;
};

#define cm_construct(type, user, hash, compare, alloc, dealloc, raw) \
  ((struct cheesemap){type, user, hash, compare, alloc, dealloc, raw})

void cm_new(struct cheesemap* map, cm_usize key_size, cm_usize key_align,
            cm_usize value_size, cm_usize value_align, cm_u8* user,
            cm_hash_fn hash, cm_compare_fn compare, cm_alloc_fn alloc,
            cm_dealloc_fn dealloc);
void cm_drop(struct cheesemap* map);
bool cm_insert(struct cheesemap* map, const cm_u8* key, const cm_u8* value);
bool cm_lookup(const struct cheesemap* map, const cm_u8* key,
               cm_u8** out_value);
bool cm_remove(struct cheesemap* map, const cm_u8* key, cm_u8* out_value);
bool cm_reserve(struct cheesemap* map, cm_usize additional);

////////////////////////////////
// cheesemap convenience macros
//

#define cm_new_(map, K, V, user, hash_fn, cmp_fn, alloc_fn, dealloc_fn)      \
  cm_new(map, sizeof(K), _Alignof(K), sizeof(V), _Alignof(V), user, hash_fn, \
         cmp_fn, alloc_fn, dealloc_fn)

#define cm_lookup_(map, key, out_val) \
  cm_lookup(map, (const cm_u8*)&(key), (cm_u8**)(out_val))

#define cm_insert_(map, key, val) \
  cm_insert(map, (const cm_u8*)&(key), (const cm_u8*)&(val))

#define cm_remove_(map, key, out_val) \
  cm_remove(map, (const cm_u8*)&(key), (cm_u8*)(out_val))

////////////////////////////////
// cheesemap iterators
//

struct cheesemap_raw_iter {
  bitmask_t curr_mask;
  cm_usize curr_index;
  cm_u8* n_ctrl;
  cm_u8* n_entry;
  cm_u8* end;
};

void cm_raw_iter_init(struct cheesemap_raw_iter* iter,
                      const struct cheesemap_raw* map,
                      const struct cm_type* type, cm_usize start_index);
bool cm_raw_iter_next(struct cheesemap_raw_iter* iter,
                      const struct cm_type* type, cm_usize* out_index);

struct cheesemap_iter {
  cm_usize entry_size, value_offset;
  struct cheesemap_raw_iter raw;
};

void cm_iter_init(struct cheesemap_iter* iter, const struct cheesemap* map);
bool cm_iter_next(struct cheesemap_iter* iter, const struct cheesemap* map,
                  cm_u8** out_key, cm_u8** out_value);

#define cm_iter_next_(iter, map, out_key, out_val) \
  cm_iter_next(iter, map, (cm_u8**)(out_key), (cm_u8**)(out_val))

#ifdef __cplusplus
}
#endif

#endif
