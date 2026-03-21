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
#error "ctz not implemented"
#endif
}

static inline uintptr_t cm_bitmask_to_index(bitmask_t mask) {
  return cm_ctz(mask) / CHAR_BIT;
}

static inline uintptr_t cm_clz(uintptr_t val) {
  assert(val != 0);
#if defined(__GNUC__) || defined(__clang__)
#if UINTPTR_MAX == UINT64_MAX
  return (uintptr_t)__builtin_clzll(val);
#elif UINTPTR_MAX == UINT32_MAX
  return (uintptr_t)__builtin_clz(val);
#else
#error "unknown word width"
#endif
#else
#error "clz not implemented"
#endif
}

#define cm_max(x, y) x > y ? x : y
#define cm_ispow2(x) (((x) & ((x) - 1)) == 0)

static inline uintptr_t cm_align_up(uintptr_t value, uintptr_t alignment) {
  assert(cm_ispow2(alignment));
  return (value + alignment - 1) & ~(alignment - 1);
}

static inline uintptr_t cm_npow2(uintptr_t v) {
  if (v <= 1) return 1;
  return (uintptr_t)1 << (CM_WORD_WIDTH - cm_clz(v - 1));
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
static inline bitmask_t cm_group_full(group_t group);

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

static inline bitmask_t cm_group_full(group_t group) {
  return cm_group_empty_or_deleted(group) ^ cm_group_repeat(CM_CTRL_DELETED);
}

/* static ctrl's */
const uint8_t CM_CTRL_STATIC_EMPTY[CM_GROUP_SIZE] = {[0 ... CM_GROUP_SIZE - 1] =
                                                         CM_CTRL_EMPTY};

/* hashmap implementation */
static inline uintptr_t cm_buckets_to_capacity(uintptr_t bucket_mask) {
  uintptr_t num_buckets = bucket_mask + 1;
  return (num_buckets / CM_LOAD_DENOM) * CM_LOAD_NUM;
}

static inline uintptr_t cm_capacity_to_buckets(uintptr_t capacity) {
  uintptr_t min_buckets =
      (capacity * CM_LOAD_DENOM + CM_LOAD_NUM - 1) / CM_LOAD_NUM;
  uintptr_t buckets = cm_npow2(min_buckets);
  return cm_max(buckets, CM_GROUP_SIZE);
}

static inline uintptr_t cm_ctrl_offset(uintptr_t buckets,
                                       uintptr_t entry_size) {
  uintptr_t offset = entry_size * buckets;
  return cm_align_up(offset, CM_GROUP_SIZE);
}

static inline uint8_t* cm_raw_elem_at(const struct cheesemap_raw* map,
                                      uintptr_t index, uintptr_t entry_size) {
  assert(map != NULL);
  assert(map->bucket_mask + 1 > index);

  return map->ctrl - entry_size * (index + 1);
}

static inline uint8_t* cm_raw_origin(const struct cheesemap_raw* map,
                                     uintptr_t entry_size) {
  assert(map != NULL);
  uintptr_t buckets = map->bucket_mask + 1;
  uintptr_t ctrl_offset = cm_ctrl_offset(buckets, entry_size);
  return map->ctrl - ctrl_offset;
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

  uintptr_t bit = cm_bitmask_to_index(mask);
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

static void cm_raw_rehash(struct cheesemap_raw* old_map,
                          struct cheesemap_raw* new_map,
                          const struct cheesemap_fns* fns, uintptr_t key_size,
                          uintptr_t value_size) {
  assert(old_map != NULL);
  assert(new_map != NULL && fns != NULL);

  uintptr_t entry_size = key_size + value_size;

  struct cheesemap_raw_iter iter;
  cm_raw_iter_init(&iter, old_map, entry_size, 0);

  uintptr_t index;
  while (cheesemap_raw_iter_next(&iter, entry_size, &index)) {
    uint8_t* elem = cm_raw_elem_at(old_map, index, entry_size);
    cm_hash_t hash = fns->hash(elem, fns->map_usr);

    uintptr_t new_index = cm_raw_find_insert_index(new_map, hash);
    cm_raw_ctrl_set(new_map, new_index, cm_h2(hash));

    uint8_t* new_elem = cm_raw_elem_at(new_map, new_index, entry_size);
    memcpy(new_elem, elem, entry_size);
  }

  new_map->count = old_map->count;
  new_map->growth_left =
      cm_buckets_to_capacity(new_map->bucket_mask) - new_map->count;
}

static bool cm_raw_resize(struct cheesemap_raw* map,
                          const struct cheesemap_fns* fns, uintptr_t key_size,
                          uintptr_t value_size, uintptr_t new_capacity) {
  assert(map != NULL && fns != NULL);

  struct cheesemap_raw new_map;
  bool success =
      cm_raw_new_with(&new_map, fns, key_size, value_size, new_capacity);
  if (!success) return false;

  cm_raw_rehash(map, &new_map, fns, key_size, value_size);

  cm_raw_drop(map, key_size, value_size, fns);
  *map = new_map;
  return true;
}

bool cm_raw_new_with(struct cheesemap_raw* map, const struct cheesemap_fns* fns,
                     uintptr_t key_size, uintptr_t value_size,
                     uintptr_t initial_capacity) {
  assert(map != NULL);
  memset(map, 0, sizeof(*map));

  uintptr_t buckets = cm_capacity_to_buckets(initial_capacity);
  uintptr_t entry_size = key_size + value_size;
  uintptr_t ctrl_offset = cm_ctrl_offset(buckets, entry_size);
  uintptr_t size = ctrl_offset + buckets + CM_GROUP_SIZE;

  uint8_t* ptr = fns->malloc(size, fns->mem_usr);
  if (ptr == NULL) return false;

  uint8_t* ctrl = ptr + ctrl_offset;
  memset(ctrl, CM_CTRL_EMPTY, buckets);
  memcpy(ctrl + buckets, ctrl, CM_GROUP_SIZE);

  map->bucket_mask = buckets - 1;
  map->growth_left = cm_buckets_to_capacity(map->bucket_mask);
  map->ctrl = ctrl;

  return true;
}

bool cm_raw_reserve(struct cheesemap_raw* map, const struct cheesemap_fns* fns,
                    uintptr_t key_size, uintptr_t value_size,
                    uintptr_t additional) {
  assert(map != NULL && fns != NULL);

  if (map->growth_left >= additional) return true;
  // TODO: inplace rehash
  uintptr_t needed = map->count + additional;
  uintptr_t capacity = cm_buckets_to_capacity(map->bucket_mask);
  uintptr_t new_capacity = cm_max(needed, capacity + 1);
  return cm_raw_resize(map, fns, key_size, value_size, new_capacity);
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
    bool success = cm_raw_reserve(map, fns, key_size, value_size, 1);
    if (!success) return success;
    index = cm_raw_find_insert_index(map, hash);
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

void cm_drop(struct cheesemap* map) {
  assert(map != NULL);

  cm_raw_drop(&map->raw, map->key_size, map->value_size, &map->fns);
  memset(map, 0, sizeof(*map));
}

/* iterator */
static inline uint8_t* cm_raw_iter_next_entry(
    const struct cheesemap_raw_iter* iter, uint8_t* old_entry,
    uintptr_t entry_size) {
  assert(iter != NULL);
  return old_entry - entry_size * CM_GROUP_SIZE;
}

void cm_raw_iter_init(struct cheesemap_raw_iter* iter,
                      const struct cheesemap_raw* map, uintptr_t entry_size,
                      uintptr_t start_index) {
  assert(map != NULL);
  assert(start_index % CM_GROUP_SIZE == 0);
  memset(iter, 0, sizeof(*iter));

  uintptr_t buckets = map->bucket_mask + 1;
  assert(buckets > start_index);

  uint8_t* ctrl = &map->ctrl[start_index];
  uint8_t* entry = cm_raw_elem_at(map, start_index, entry_size);

  group_t group = cm_group_load(ctrl);
  bitmask_t mask = cm_group_full(group);

  iter->curr_mask = mask;
  iter->curr_index = start_index;
  iter->n_ctrl = ctrl + CM_GROUP_SIZE;
  iter->n_entry = cm_raw_iter_next_entry(iter, entry, entry_size);
  iter->end = map->ctrl + buckets;
}

static inline void cm_raw_iter_next_inner_slow(
    struct cheesemap_raw_iter* iter) {
  assert(iter != NULL);

  group_t group = cm_group_load(iter->n_ctrl);
  iter->n_ctrl += CM_GROUP_SIZE;
  iter->curr_mask = cm_group_full(group);
  iter->curr_index += CM_GROUP_SIZE;
}

static inline uintptr_t cm_raw_iter_next_inner_fast(
    struct cheesemap_raw_iter* iter) {
  assert(iter != NULL);

  uintptr_t bit = cm_bitmask_to_index(iter->curr_mask);
  iter->curr_mask &= iter->curr_mask - 1;

  return iter->curr_index + bit;
}

bool cheesemap_raw_iter_next(struct cheesemap_raw_iter* iter,
                             uintptr_t entry_size, uintptr_t* out_index) {
  assert(iter != NULL);
  assert(out_index != NULL);

  while (true) {
    if (iter->curr_mask != 0) {
      *out_index = cm_raw_iter_next_inner_fast(iter);
      return true;
    }

    if (iter->n_ctrl >= iter->end) return false;

    cm_raw_iter_next_inner_slow(iter);
    iter->n_entry = cm_raw_iter_next_entry(iter, iter->n_entry, entry_size);
  }
}
