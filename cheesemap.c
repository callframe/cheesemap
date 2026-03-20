#include "cheesemap.h"

#include <stddef.h>

static inline uintptr_t cm_raw_capacity(const struct cheesemap_raw* map) {
  assert(map != NULL);
  return map->cap_mask + 1;
}

static inline uint8_t* cm_raw_origin(const struct cheesemap_raw* map,
                                     uintptr_t entry_size) {
  assert(map != NULL);
  return map->ctrl - entry_size * cm_raw_capacity(map);
}

void cm_raw_drop(struct cheesemap_raw* map, uintptr_t entry_size,
                 struct cheesemap_fns* fns) {
  assert(map != NULL);
  assert(fns != NULL);

  if (map->ctrl == NULL) return;

  uint8_t* origin = cm_raw_origin(map, entry_size);
  fns->free(origin, fns->mem_usr);
}

void cm_new(struct cheesemap* map, uintptr_t entry_size,
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

