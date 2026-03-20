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
