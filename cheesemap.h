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

_Noreturn void CM_OPT_PANIC_NAME(const char* file, uint32_t line,
                                 const char* fmt, ...);

#define cm_assert(cond)                                                        \
  do {                                                                         \
    if (!(cond))                                                               \
      CM_OPT_PANIC_NAME(__FILE__, __LINE__, "cm_assertion failed: %s", #cond); \
  } while (0)

#ifdef CM_OPT_ENABLE_SSE2
#include <emmintrin.h>

typedef __m128i group_t;
typedef uint16_t bitmask_t;
#define CM_GROUP_SIZE 16
#define CM_NO_FALLBACK
#endif

#ifndef CM_NO_FALLBACK
typedef uintptr_t group_t;
typedef group_t bitmask_t;
#define CM_GROUP_SIZE __SIZEOF_POINTER__
#endif

////////////////////////////////
// cheesemap callback functions
//

typedef uint64_t cm_hash_t;

/* hash and compare methods */
typedef cm_hash_t (*cm_hash_fn)(const uint8_t* key, uint8_t* user);
typedef bool (*cm_compare_fn)(const uint8_t* key1, const uint8_t* key2,
                              uint8_t* user);

/* allocator methods */
typedef void* (*cm_alloc_fn)(uintptr_t size, uint8_t* user);
typedef void (*cm_dealloc_fn)(void* ptr, uint8_t* user);

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
  CM_WORD_WIDTH = sizeof(uintptr_t) * CHAR_BIT,

};

extern const uint8_t CM_CTRL_STATIC_EMPTY[CM_GROUP_SIZE];

struct cm_type {
  uintptr_t key_size;
  uintptr_t value_size;
  uintptr_t value_offset;
  uintptr_t entry_size;
};

#define cm_type_construct(key_size, value_size, value_offset, entry_size) \
  ((struct cm_type){key_size, value_size, value_offset, entry_size})

struct cheesemap_raw {
  // number of buckets as mask
  uintptr_t bucket_mask;
  // number of entries in the map
  uintptr_t count;
  // number of entry left until resize
  uintptr_t growth_left;
  // pointer to the control bytes
  uint8_t* ctrl;
};

#define cm_raw_new() \
  ((struct cheesemap_raw){.ctrl = (uint8_t*)CM_CTRL_STATIC_EMPTY})

bool cm_raw_new_with(struct cheesemap_raw* map, cm_alloc_fn alloc,
                     uint8_t* user, const struct cm_type* type,
                     uintptr_t initial_capacity);
void cm_raw_drop(struct cheesemap_raw* map, cm_dealloc_fn dealloc,
                 uint8_t* user, const struct cm_type* type);
bool cm_raw_reserve(struct cheesemap_raw* map, cm_hash_fn hash,
                    cm_alloc_fn alloc, cm_dealloc_fn dealloc, uint8_t* user,
                    const struct cm_type* type, uintptr_t additional);
bool cm_raw_lookup(const struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, uint8_t* user,
                   const struct cm_type* type, const uint8_t* key,
                   uint8_t** out_value);
bool cm_raw_remove(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, uint8_t* user,
                   const struct cm_type* type, const uint8_t* key,
                   uint8_t* out_value);
bool cm_raw_insert(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_alloc_fn alloc, cm_dealloc_fn dealloc, uint8_t* user,
                   const struct cm_type* type, const uint8_t* key,
                   const uint8_t* value);

////////////////////////////////
// cheesemap implementation
//

struct cheesemap {
  struct cm_type type;
  uint8_t* user;
  cm_hash_fn hash;
  cm_compare_fn compare;
  cm_alloc_fn alloc;
  cm_dealloc_fn dealloc;
  struct cheesemap_raw raw;
};

#define cm_construct(type, user, hash, compare, alloc, dealloc, raw) \
  ((struct cheesemap){type, user, hash, compare, alloc, dealloc, raw})

void cm_new(struct cheesemap* map, uintptr_t key_size, uintptr_t key_align,
            uintptr_t value_size, uintptr_t value_align, uint8_t* user,
            cm_hash_fn hash, cm_compare_fn compare, cm_alloc_fn alloc,
            cm_dealloc_fn dealloc);
void cm_drop(struct cheesemap* map);
bool cm_insert(struct cheesemap* map, const uint8_t* key, const uint8_t* value);
bool cm_lookup(const struct cheesemap* map, const uint8_t* key,
               uint8_t** out_value);
bool cm_remove(struct cheesemap* map, const uint8_t* key, uint8_t* out_value);
bool cm_reserve(struct cheesemap* map, uintptr_t additional);

////////////////////////////////
// cheesemap convenience macros
//

#define cm_new_(map, K, V, user, hash_fn, cmp_fn, alloc_fn, dealloc_fn)      \
  cm_new(map, sizeof(K), _Alignof(K), sizeof(V), _Alignof(V), user, hash_fn, \
         cmp_fn, alloc_fn, dealloc_fn)

#define cm_lookup_(map, key, out_val) \
  cm_lookup(map, (const uint8_t*)&(key), (uint8_t**)(out_val))

#define cm_insert_(map, key, val) \
  cm_insert(map, (const uint8_t*)&(key), (const uint8_t*)&(val))

#define cm_remove_(map, key, out_val) \
  cm_remove(map, (const uint8_t*)&(key), (uint8_t*)(out_val))

////////////////////////////////
// cheesemap iterators
//

struct cheesemap_raw_iter {
  bitmask_t curr_mask;
  uintptr_t curr_index;
  uint8_t* n_ctrl;
  uint8_t* n_entry;
  uint8_t* end;
};

void cm_raw_iter_init(struct cheesemap_raw_iter* iter,
                      const struct cheesemap_raw* map,
                      const struct cm_type* type, uintptr_t start_index);
bool cm_raw_iter_next(struct cheesemap_raw_iter* iter,
                      const struct cm_type* type, uintptr_t* out_index);

struct cheesemap_iter {
  uintptr_t entry_size, value_offset;
  struct cheesemap_raw_iter raw;
};

void cm_iter_init(struct cheesemap_iter* iter, const struct cheesemap* map);
bool cm_iter_next(struct cheesemap_iter* iter, const struct cheesemap* map,
                  uint8_t** out_key, uint8_t** out_value);

#define cm_iter_next_(iter, map, out_key, out_val) \
  cm_iter_next(iter, map, (uint8_t**)(out_key), (uint8_t**)(out_val))

#ifdef __cplusplus
}
#endif

#endif
