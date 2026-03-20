#ifndef CHEESEMAP
#define CHEESEMAP

////////////////////////////////
// options and includes
//

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include CM_OPT_ASSERT_PATH
#ifndef assert
#error "assert is not defined"
#endif

typedef uintptr_t group_t;
typedef group_t bitmask_t;

enum {
  CM_GROUP_SIZE = sizeof(group_t),
};

////////////////////////////////
// cheesemap callback functions
//

typedef uint64_t cm_hash_t;

/* memory methods */
typedef void* (*cm_malloc_fn)(uintptr_t size, uintptr_t alignment,
                              uint8_t* user);
typedef void (*cm_free_fn)(void* ptr, uint8_t* user);

/* hash and compare methods */
typedef cm_hash_t (*cm_hash_fn)(const uint8_t* key, uint8_t* user);
typedef bool (*cm_compare_fn)(const uint8_t* key1, const uint8_t* key2,
                              uint8_t* user);

////////////////////////////////
// callback methods needed by cheesemap
//

struct cheesemap_fns {
  uint8_t *mem_usr, *map_usr;
  cm_malloc_fn malloc;
  cm_free_fn free;
  cm_hash_fn hash;
  cm_compare_fn compare;
};

////////////////////////////////
// raw cheesemap implementation
//
// layout:
// [entries...][control bits...][mirror first CM_GROUP_SIZE bits]

enum {
  CM_INITIAL_CAPACITY = CM_GROUP_SIZE,
  // -1 as i8, all bits set, top bit = 1
  CM_CTRL_EMPTY = 0xFF,
  // -128 as i8, top bit = 1
  CM_CTRL_DELETED = 0x80,
  // FULL entries have top bit = 0, lower 7 bits are H2 hash
  CM_H2_MASK = 0x7F,
  CM_FP_SIZE = 7
};

static inline uintptr_t cm_h1(cm_hash_t hash) {
  return (uintptr_t)(hash >> CM_FP_SIZE);
}

static inline uint8_t cm_h2(cm_hash_t hash) {
  return (uint8_t)(hash & CM_H2_MASK);
}

extern const uint8_t CM_CTRL_STATIC_EMPTY[CM_GROUP_SIZE];

struct cheesemap_raw {
  // number of buckets as mask
  uintptr_t bucket_mask;
  // number of entries in the map
  uintptr_t entry_count;
  // number of entry left until resize
  uintptr_t growth_left;
  // pointer to the control bytes
  uint8_t* ctrl;
};

#define cm_raw_new() \
  ((struct cheesemap_raw){.ctrl = (uint8_t*)CM_CTRL_STATIC_EMPTY})

void cm_raw_insert(struct cheesemap_raw* map, const struct cheesemap_fns* fns,
                   const void* key, const void* value);
void cm_raw_drop(struct cheesemap_raw* map, uintptr_t entry_size,
                 const struct cheesemap_fns* fns);

////////////////////////////////
// cheesemap implementation
//

struct cheesemap {
  uintptr_t entry_size;
  struct cheesemap_fns fns;
  struct cheesemap_raw raw;
};

void cm_new(struct cheesemap* map, uintptr_t entry_size, uint8_t* mem_usr,
            cm_malloc_fn malloc, cm_free_fn free, uint8_t* map_usr,
            cm_hash_fn hash, cm_compare_fn compare);

static inline void cm_drop(struct cheesemap* map) {
  assert(map != NULL);

  cm_raw_drop(&map->raw, map->entry_size, &map->fns);
  memset(map, 0, sizeof(*map));
}

#endif
