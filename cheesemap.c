#include "cheesemap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uintptr_t cm_ctz(uintptr_t val) {
  assert(val != 0);
#if defined(__GNUC__) || defined(__clang__)
#if UINTPTR_MAX == UINT64_MAX
  return (uintptr_t)__builtin_ctzll(val);
#elif UINTPTR_MAX == UINT32_MAX
  return (uintptr_t)__builtin_ctz(val);
#else
#error "unknown word width"
#endif
#else
#error "ctz not implemented for this compiler"
#endif
}

struct sequence {
  uintptr_t pos;
  uintptr_t stride;
};

#define sequence_init(pos, stride) ((struct sequence){pos, stride})

static inline void cm_sequence_next(struct sequence* sequence,
                                    uintptr_t bucket_mask) {
  assert(sequence != NULL);
  assert(sequence->stride <= bucket_mask);

  sequence->stride += CM_GROUP_SIZE;
  sequence->pos += sequence->stride;
  sequence->pos &= bucket_mask;
}

/* ctrl ops */
static inline bool cm_ctrl_is_special(uint8_t v) { return v & CM_CTRL_DELETED; }

static inline bool cm_ctrl_is_empty(uint8_t v) {
  assert(cm_ctrl_is_special(v) == true);
  return (v & CM_CTRL_END) != 0;
}

/* group ops */
static inline group_t cm_group_load(const uint8_t* ctrl);
static inline bitmask_t cm_group_empty_or_deleted(group_t group);

/* scalar implementation */
static inline group_t cm_group_repeat(uint8_t v) {
  return (group_t)v * (((group_t)-1) / (uint8_t)~0);
}

static inline group_t cm_group_load(const uint8_t* ctrl) {
  assert(ctrl != NULL);

  group_t v;
  memcpy(&v, ctrl, sizeof(v));
  return v;
}

static inline bitmask_t cm_group_empty_or_deleted(group_t group) {
  return group & cm_group_repeat(CM_CTRL_DELETED);
}

/* static ctrl's */
const uint8_t CM_CTRL_STATIC_EMPTY[CM_GROUP_SIZE] = {[0 ... CM_GROUP_SIZE - 1] =
                                                         CM_CTRL_EMPTY};

/* hashmap implementation */
static inline uint8_t* cm_raw_elem_at(const struct cheesemap_raw* map,
                                      uintptr_t index, uintptr_t entry_size) {
  assert(map != NULL);
  assert(map->bucket_mask + 1 > index);

  return map->ctrl - entry_size * index;
}

static inline uint8_t* cm_raw_origin(const struct cheesemap_raw* map,
                                     uintptr_t entry_size) {
  assert(map != NULL);
  return cm_raw_elem_at(map, map->bucket_mask + 1, entry_size);
}

static inline void cm_raw_ctrl_set(struct cheesemap_raw* map, uintptr_t index,
                                   uint8_t ctrl) {
  assert(map != NULL);

  uintptr_t index2 =
      ((index - CM_GROUP_SIZE) & map->bucket_mask) + CM_GROUP_SIZE;
  map->ctrl[index] = ctrl;
  map->ctrl[index2] = ctrl;
}

static bool cm_raw_find_insert_index_in_group(const struct cheesemap_raw* map,
                                              const group_t* group,
                                              const struct sequence* seq,
                                              uintptr_t* out_index) {
  assert(map != NULL);
  assert(group != NULL && seq != NULL);
  assert(out_index != NULL);

  bitmask_t mask = cm_group_empty_or_deleted(*group);
  if (mask == 0) return false;

  uintptr_t bit = cm_ctz(mask);
  *out_index = (seq->pos + bit) & map->bucket_mask;
  return true;
}

static uintptr_t cm_raw_find_insert_index(const struct cheesemap_raw* map,
                                          cm_hash_t hash) {
  assert(map != NULL);

  struct sequence seq = sequence_init(cm_h1(hash) & map->bucket_mask, 0);
  while (true) {
    uint8_t* ctrl = &map->ctrl[seq.pos];
    group_t group = cm_group_load(ctrl);

    uintptr_t index;
    if (cm_raw_find_insert_index_in_group(map, &group, &seq, &index))
      return index;
    cm_sequence_next(&seq, map->bucket_mask);
  }
}

static void cm_raw_insert_at(struct cheesemap_raw* map, cm_hash_t hash,
                             uintptr_t index, uintptr_t key_size,
                             const uint8_t* key, uintptr_t value_size,
                             const uint8_t* value) {
  assert(map != NULL);
  assert(value != NULL);

  uint8_t old_ctrl = map->ctrl[index];
  map->growth_left -= cm_ctrl_is_empty(old_ctrl);
  cm_raw_ctrl_set(map, index, cm_h2(hash));

  uint8_t* elem = cm_raw_elem_at(map, index, key_size + value_size);
  memcpy(elem, key, key_size);
  elem += key_size;
  memcpy(elem, value, value_size);

  map->count += 1;
}

bool cm_raw_insert(struct cheesemap_raw* map, const struct cheesemap_fns* fns,
                   uintptr_t key_size, const uint8_t* key, uintptr_t value_size,
                   const uint8_t* value) {
  assert(map != NULL && fns != NULL);
  assert(key != NULL && value != NULL);

  cm_hash_t hash = fns->hash(key, fns->map_usr);
  uintptr_t index = cm_raw_find_insert_index(map, hash);

  uint8_t old_ctrl = map->ctrl[index];
  if (map->growth_left == 0 && cm_ctrl_is_empty(old_ctrl)) {
    // TODO: do resize
  }

  cm_raw_insert_at(map, hash, index, key_size, key, value_size, value);
  return true;
}

void cm_raw_drop(struct cheesemap_raw* map, uintptr_t key_size,
                 uintptr_t value_size, const struct cheesemap_fns* fns) {
  assert(map != NULL);
  assert(fns != NULL);

  if (map->ctrl == CM_CTRL_STATIC_EMPTY || map->ctrl == NULL) return;

  uint8_t* origin = cm_raw_origin(map, key_size + value_size);
  fns->free(origin, fns->mem_usr);

  *map = cm_raw_new();
}

void cm_new(struct cheesemap* map, uintptr_t key_size, uintptr_t value_size,
            uint8_t* mem_usr, cm_malloc_fn malloc, cm_free_fn free,
            uint8_t* map_usr, cm_hash_fn hash, cm_compare_fn compare) {
  assert(map != NULL);
  assert(malloc != NULL && free != NULL);
  assert(hash != NULL && compare != NULL);

  struct cheesemap_fns fns = {
      mem_usr, map_usr,  //
      malloc,  free,     //
      hash,    compare,  //
  };
  *map = (struct cheesemap){key_size, value_size, fns, cm_raw_new()};
}
