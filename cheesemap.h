#ifndef CHEESEMAP
#define CHEESEMAP

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////
// options and includes
//

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef CM_SSE2
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

bool cm_raw_new_with(struct cheesemap_raw* map, uintptr_t entry_size,
                     uintptr_t initial_capacity);
void cm_raw_drop(struct cheesemap_raw* map, uintptr_t entry_size);
bool cm_raw_reserve(struct cheesemap_raw* map, cm_hash_fn hash, uint8_t* user,
                    uintptr_t entry_size, uintptr_t additional);
bool cm_raw_lookup(const struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, uint8_t* user, uintptr_t entry_size,
                   uintptr_t value_offset, const uint8_t* key,
                   uint8_t** out_value);
bool cm_raw_remove(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, uint8_t* user, uintptr_t entry_size,
                   uintptr_t value_offset, uintptr_t value_size,
                   const uint8_t* key, uint8_t* out_value);
bool cm_raw_insert(struct cheesemap_raw* map, cm_hash_fn hash, uint8_t* user,
                   uintptr_t entry_size, uintptr_t key_size,
                   uintptr_t value_offset, uintptr_t value_size,
                   const uint8_t* key, const uint8_t* value);

////////////////////////////////
// cheesemap implementation
//

struct cheesemap {
  uintptr_t key_size, value_size;
  uintptr_t value_offset, entry_size;
  uint8_t* user;
  cm_hash_fn hash;
  cm_compare_fn compare;
  struct cheesemap_raw raw;
};

void cm_new(struct cheesemap* map, uintptr_t key_size, uintptr_t key_align,
            uintptr_t value_size, uintptr_t value_align, uint8_t* user,
            cm_hash_fn hash, cm_compare_fn compare);
void cm_drop(struct cheesemap* map);
bool cm_insert(struct cheesemap* map, const uint8_t* key, const uint8_t* value);
bool cm_lookup(const struct cheesemap* map, const uint8_t* key,
               uint8_t** out_value);
bool cm_remove(struct cheesemap* map, const uint8_t* key, uint8_t* out_value);
bool cm_reserve(struct cheesemap* map, uintptr_t additional);

////////////////////////////////
// cheesemap convenience macros
//

#define cm_new_(map, K, V, user, hash_fn, cmp_fn)                            \
  cm_new(map, sizeof(K), _Alignof(K), sizeof(V), _Alignof(V), user, hash_fn, \
         cmp_fn)

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
                      const struct cheesemap_raw* map, uintptr_t entry_size,
                      uintptr_t start_index);
bool cm_raw_iter_next(struct cheesemap_raw_iter* iter, uintptr_t entry_size,
                      uintptr_t* out_index);

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
