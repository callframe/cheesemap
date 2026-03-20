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

enum {
  CM_GROUP_SIZE = 8,
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
  CM_CTRL_EMPTY = 0x80,
  CM_CTRL_DELETED = 0xFE,
  CM_H2_MASK = 0x7F,
  CM_FP_SIZE = 7
};

static inline uintptr_t cm_h1(cm_hash_t hash) {
  return (uintptr_t)(hash >> CM_FP_SIZE);
}

static inline uint8_t cm_h2(cm_hash_t hash) {
  return (uint8_t)(hash & CM_H2_MASK);
}

struct cheesemap_raw {
  // mask of the capacity
  uintptr_t cap_mask;
  // number of entries in the map
  uintptr_t entry_count;
  // number of entry left until resize
  uintptr_t growth_left;
  // pointer to the control bytes
  uint8_t* ctrl;
};

#define cm_raw_new() ((struct cheesemap_raw){0})

void cm_raw_drop(struct cheesemap_raw* map, uintptr_t entry_size,
                 struct cheesemap_fns* fns);

////////////////////////////////
// cheesemap implementation
//

struct cheesemap {
  uintptr_t entry_size;
  struct cheesemap_fns fns;
  struct cheesemap_raw raw;
};

static inline void cm_new(struct cheesemap* map, uintptr_t entry_size,
                          uint8_t* mem_usr, cm_malloc_fn malloc, cm_free_fn free,
                          uint8_t* map_usr, cm_hash_fn hash,
                          cm_compare_fn compare) {
  assert(map != NULL);
  assert(malloc != NULL && free != NULL);
  assert(hash != NULL && compare != NULL);

  struct cheesemap_fns fns = {
      mem_usr, map_usr,  //
      malloc,  free,     //
      hash,    compare,  //
  };

  *map = (struct cheesemap){entry_size, fns, cm_raw_new()};
}

static inline void cm_drop(struct cheesemap* map) {
  assert(map != NULL);

  cm_raw_drop(&map->raw, map->entry_size, &map->fns);
  memset(map, 0, sizeof(*map));
}

#endif
