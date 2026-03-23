#include "cheesemap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CM_ATTR(...) __attribute__((__VA_ARGS__))

CM_ATTR(hot) static inline uintptr_t cm_ctz(uintptr_t val) {
  cm_assert(val != 0);
#if UINTPTR_MAX == UINT64_MAX
  return (uintptr_t)__builtin_ctzll(val);
#elif UINTPTR_MAX == UINT32_MAX
  return (uintptr_t)__builtin_ctz(val);
#else
#error "unknown word width"
#endif
}

CM_ATTR(hot) static inline uintptr_t cm_clz(uintptr_t val) {
  cm_assert(val != 0);
#if UINTPTR_MAX == UINT64_MAX
  return (uintptr_t)__builtin_clzll(val);
#elif UINTPTR_MAX == UINT32_MAX
  return (uintptr_t)__builtin_clz(val);
#else
#error "unknown word width"
#endif
}

CM_ATTR(hot) static inline uintptr_t cm_bitmask_lowest_set_bit(bitmask_t mask) {
#if CM_GROUP_SIZE == 8
  return cm_ctz(mask) / CHAR_BIT;
#elif CM_GROUP_SIZE == 16
  return cm_ctz(mask);
#else
#error "unknown group size"
#endif
}

CM_ATTR(hot) static inline uintptr_t cm_bitmask_ctz(bitmask_t mask) {
  if (mask == 0) return CM_GROUP_SIZE;
#if CM_GROUP_SIZE == 8
  return cm_ctz(mask) / CHAR_BIT;
#elif CM_GROUP_SIZE == 16
  return cm_ctz(mask);
#else
#error "unknown group size"
#endif
}

CM_ATTR(hot) static inline uintptr_t cm_bitmask_clz(bitmask_t mask) {
  if (mask == 0) return CM_GROUP_SIZE;
#if CM_GROUP_SIZE == 8
  return cm_clz(mask) / CHAR_BIT;
#elif CM_GROUP_SIZE == 16
  return cm_clz(mask);
#else
#error "unknown group size"
#endif
}

#define cm_max(x, y) x > y ? x : y
#define cm_ispow2(x) (((x) & ((x) - 1)) == 0)

static inline uintptr_t cm_align_up(uintptr_t value, uintptr_t alignment) {
  cm_assert(cm_ispow2(alignment));
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
  cm_assert(sequence != NULL);
  cm_assert(sequence->stride <= bucket_mask);

  sequence->stride += CM_GROUP_SIZE;
  sequence->pos += sequence->stride;
  sequence->pos &= bucket_mask;
}

/* ctrl ops */
static inline bool cm_ctrl_is_special(uint8_t v) { return v & CM_CTRL_DELETED; }

static inline bool cm_ctrl_is_empty(uint8_t v) {
  cm_assert(cm_ctrl_is_special(v) == true);
  return (v & CM_CTRL_END) != 0;
}

/* group ops */
static inline group_t cm_group_load(const uint8_t* ctrl);
static inline bitmask_t cm_group_match_tag(group_t group, uint8_t tag);
static inline bitmask_t cm_group_match_empty_or_deleted(group_t group);
static inline bitmask_t cm_group_match_empty(group_t group);
static inline bitmask_t cm_group_match_full(group_t group);

/* sse2 implementation */
#ifdef CM_OPT_ENABLE_SSE2
static inline group_t cm_group_load(const uint8_t* ctrl) {
  cm_assert(ctrl != NULL);
  return _mm_loadu_si128((const group_t*)ctrl);
}

static inline bitmask_t cm_group_match_tag(group_t group, uint8_t tag) {
  const __m128i tagvec = _mm_set1_epi8(tag);
  __m128i cmp = _mm_cmpeq_epi8(group, tagvec);
  return _mm_movemask_epi8(cmp);
}

static inline bitmask_t cm_group_match_empty_or_deleted(group_t group) {
  return _mm_movemask_epi8(group);
}

static inline bitmask_t cm_group_match_empty(group_t group) {
  return cm_group_match_tag(group, CM_CTRL_EMPTY);
}

static inline bitmask_t cm_group_match_full(group_t group) {
  return ~cm_group_match_empty_or_deleted(group);
}
#endif

/* scalar implementation */
#ifndef CM_NO_FALLBACK
static inline group_t cm_group_repeat(uint8_t v) {
  return (group_t)v * (((group_t)-1) / (uint8_t)~0);
}

static inline group_t cm_group_load(const uint8_t* ctrl) {
  cm_assert(ctrl != NULL);

  group_t v;
  memcpy(&v, ctrl, sizeof(v));
  return v;
}

static inline bitmask_t cm_group_match_empty_or_deleted(group_t group) {
  return group & cm_group_repeat(CM_CTRL_DELETED);
}

static inline bitmask_t cm_group_match_empty(group_t group) {
  return (group & (group << 1)) & cm_group_repeat(CM_CTRL_DELETED);
}

static inline bitmask_t cm_group_match_full(group_t group) {
  return cm_group_match_empty_or_deleted(group) ^
         cm_group_repeat(CM_CTRL_DELETED);
}

static inline bitmask_t cm_group_match_tag(group_t group, uint8_t tag) {
  group_t cmp = group ^ cm_group_repeat(tag);
  return (cmp - cm_group_repeat(CM_CTRL_END)) & ~cmp &
         cm_group_repeat(CM_CTRL_DELETED);
}
#endif

/* ctrl's n stuff */

static inline uintptr_t cm_h1(cm_hash_t hash) {
  return (uintptr_t)(hash >> CM_FP_SIZE);
}

static inline uint8_t cm_h2(cm_hash_t hash) {
  uintptr_t top = hash >> (sizeof(cm_hash_t) * CHAR_BIT - CM_FP_SIZE);
  return (uint8_t)(top & CM_H2_MASK);
}

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
  cm_assert(map != NULL);
  cm_assert(map->bucket_mask + 1 > index);

  return map->ctrl - entry_size * (index + 1);
}

static inline uint8_t* cm_raw_origin(const struct cheesemap_raw* map,
                                     uintptr_t entry_size) {
  cm_assert(map != NULL);
  uintptr_t buckets = map->bucket_mask + 1;
  uintptr_t ctrl_offset = cm_ctrl_offset(buckets, entry_size);
  return map->ctrl - ctrl_offset;
}

static inline void cm_raw_ctrl_set(struct cheesemap_raw* map, uintptr_t index,
                                   uint8_t ctrl) {
  cm_assert(map != NULL);

  uintptr_t index2 =
      ((index - CM_GROUP_SIZE) & map->bucket_mask) + CM_GROUP_SIZE;
  map->ctrl[index] = ctrl;
  map->ctrl[index2] = ctrl;
}

static bool cm_raw_find_insert_index_in_group(const struct cheesemap_raw* map,
                                              const group_t* group,
                                              const struct sequence* seq,
                                              uintptr_t* out_index) {
  cm_assert(map != NULL);
  cm_assert(group != NULL && seq != NULL);
  cm_assert(out_index != NULL);

  bitmask_t mask = cm_group_match_empty_or_deleted(*group);
  if (mask == 0) return false;

  uintptr_t bucket_offset = cm_bitmask_lowest_set_bit(mask);
  *out_index = (seq->pos + bucket_offset) & map->bucket_mask;
  return true;
}

static uintptr_t cm_raw_find_insert_index(const struct cheesemap_raw* map,
                                          cm_hash_t hash) {
  cm_assert(map != NULL);

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

static bool cm_raw_find_in_group(const struct cheesemap_raw* map,
                                 cm_compare_fn compare, uint8_t* user,
                                 group_t group, const struct sequence* seq,
                                 uint8_t h2, uintptr_t entry_size,
                                 const uint8_t* key, uintptr_t* out_index) {
  cm_assert(map != NULL && compare != NULL);
  cm_assert(seq != NULL);
  cm_assert(key != NULL && out_index != NULL);

  bitmask_t mask = cm_group_match_tag(group, h2);
  while (mask != 0) {
    uintptr_t bucket_offset = cm_bitmask_lowest_set_bit(mask);
    uintptr_t index = (seq->pos + bucket_offset) & map->bucket_mask;

    uint8_t* elem = cm_raw_elem_at(map, index, entry_size);
    if (compare(key, elem, user)) {
      *out_index = index;
      return true;
    }

    mask &= mask - 1;
  }

  return false;
}

static bool cm_raw_find(const struct cheesemap_raw* map, cm_compare_fn compare,
                        uint8_t* user, cm_hash_t hash, uintptr_t entry_size,
                        const uint8_t* key, uintptr_t* out_index) {
  cm_assert(map != NULL && compare != NULL);
  cm_assert(key != NULL && out_index != NULL);

  uint8_t h2 = cm_h2(hash);
  struct sequence seq = sequence_init(cm_h1(hash) & map->bucket_mask, 0);

  while (true) {
    uint8_t* ctrl = &map->ctrl[seq.pos];
    group_t group = cm_group_load(ctrl);

    if (cm_raw_find_in_group(map, compare, user, group, &seq, h2, entry_size,
                             key, out_index))
      return true;

    if (cm_group_match_empty(group) != 0) return false;

    cm_sequence_next(&seq, map->bucket_mask);
  }
}

static void cm_raw_insert_at(struct cheesemap_raw* map, cm_hash_t hash,
                             uintptr_t index, uintptr_t entry_size,
                             uintptr_t key_size, uintptr_t value_offset,
                             uintptr_t value_size, const uint8_t* key,
                             const uint8_t* value) {
  cm_assert(map != NULL);
  cm_assert(value != NULL);

  uint8_t old_ctrl = map->ctrl[index];
  map->growth_left -= cm_ctrl_is_empty(old_ctrl);
  cm_raw_ctrl_set(map, index, cm_h2(hash));

  uint8_t* elem = cm_raw_elem_at(map, index, entry_size);
  memcpy(elem, key, key_size);
  memcpy(elem + value_offset, value, value_size);

  map->count += 1;
}

static void cm_raw_rehash(struct cheesemap_raw* old_map,
                          struct cheesemap_raw* new_map, cm_hash_fn hash,
                          uint8_t* user, uintptr_t entry_size) {
  cm_assert(old_map != NULL);
  cm_assert(new_map != NULL && hash != NULL);

  struct cheesemap_raw_iter iter;
  cm_raw_iter_init(&iter, old_map, entry_size, 0);

  uintptr_t index;
  while (cm_raw_iter_next(&iter, entry_size, &index)) {
    uint8_t* elem = cm_raw_elem_at(old_map, index, entry_size);
    cm_hash_t h = hash(elem, user);

    uintptr_t new_index = cm_raw_find_insert_index(new_map, h);
    cm_raw_ctrl_set(new_map, new_index, cm_h2(h));

    uint8_t* new_elem = cm_raw_elem_at(new_map, new_index, entry_size);
    memcpy(new_elem, elem, entry_size);
  }

  new_map->count = old_map->count;
  new_map->growth_left =
      cm_buckets_to_capacity(new_map->bucket_mask) - new_map->count;
}

static bool cm_raw_resize(struct cheesemap_raw* map, cm_hash_fn hash,
                          uint8_t* user, uintptr_t entry_size,
                          uintptr_t new_capacity) {
  cm_assert(map != NULL && hash != NULL);

  struct cheesemap_raw new_map;
  bool success = cm_raw_new_with(&new_map, entry_size, new_capacity);
  if (!success) return false;

  cm_raw_rehash(map, &new_map, hash, user, entry_size);

  cm_raw_drop(map, entry_size);
  *map = new_map;
  return true;
}

bool cm_raw_new_with(struct cheesemap_raw* map, uintptr_t entry_size,
                     uintptr_t initial_capacity) {
  cm_assert(map != NULL);
  memset(map, 0, sizeof(*map));

  uintptr_t buckets = cm_capacity_to_buckets(initial_capacity);
  uintptr_t ctrl_offset = cm_ctrl_offset(buckets, entry_size);
  uintptr_t size = ctrl_offset + buckets + CM_GROUP_SIZE;

  uint8_t* ptr = malloc(size);
  if (ptr == NULL) return false;

  uint8_t* ctrl = ptr + ctrl_offset;
  memset(ctrl, CM_CTRL_EMPTY, buckets);
  memcpy(ctrl + buckets, ctrl, CM_GROUP_SIZE);

  map->bucket_mask = buckets - 1;
  map->growth_left = cm_buckets_to_capacity(map->bucket_mask);
  map->ctrl = ctrl;

  return true;
}

bool cm_raw_reserve(struct cheesemap_raw* map, cm_hash_fn hash, uint8_t* user,
                    uintptr_t entry_size, uintptr_t additional) {
  cm_assert(map != NULL && hash != NULL);

  if (map->growth_left >= additional) return true;
  // TODO: inplace rehash
  uintptr_t needed = map->count + additional;
  uintptr_t capacity = cm_buckets_to_capacity(map->bucket_mask);
  uintptr_t new_capacity = cm_max(needed, capacity + 1);
  return cm_raw_resize(map, hash, user, entry_size, new_capacity);
}

bool cm_raw_lookup(const struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, uint8_t* user, uintptr_t entry_size,
                   uintptr_t value_offset, const uint8_t* key,
                   uint8_t** out_value) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(key != NULL && out_value != NULL);

  cm_hash_t h = hash(key, user);
  uintptr_t index;

  if (!cm_raw_find(map, compare, user, h, entry_size, key, &index))
    return false;

  uint8_t* elem = cm_raw_elem_at(map, index, entry_size);
  *out_value = elem + value_offset;
  return true;
}

bool cm_raw_remove(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, uint8_t* user, uintptr_t entry_size,
                   uintptr_t value_offset, uintptr_t value_size,
                   const uint8_t* key, uint8_t* out_value) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(key != NULL);

  cm_hash_t h = hash(key, user);
  uintptr_t index;

  if (!cm_raw_find(map, compare, user, h, entry_size, key, &index))
    return false;

  if (out_value != NULL) {
    uint8_t* elem = cm_raw_elem_at(map, index, entry_size);
    memcpy(out_value, elem + value_offset, value_size);
  }

  uintptr_t index_before = (index - CM_GROUP_SIZE) & map->bucket_mask;
  group_t group_before = cm_group_load(&map->ctrl[index_before]);
  group_t group_at = cm_group_load(&map->ctrl[index]);

  bitmask_t empty_before = cm_group_match_empty(group_before);
  bitmask_t empty_after = cm_group_match_empty(group_at);

  uintptr_t empty_count =
      cm_bitmask_clz(empty_before) + cm_bitmask_ctz(empty_after);
  uint8_t ctrl =
      (empty_count >= CM_GROUP_SIZE) ? CM_CTRL_DELETED : CM_CTRL_EMPTY;

  if (ctrl == CM_CTRL_EMPTY) map->growth_left += 1;

  cm_raw_ctrl_set(map, index, ctrl);
  map->count -= 1;

  return true;
}

bool cm_raw_insert(struct cheesemap_raw* map, cm_hash_fn hash, uint8_t* user,
                   uintptr_t entry_size, uintptr_t key_size,
                   uintptr_t value_offset, uintptr_t value_size,
                   const uint8_t* key, const uint8_t* value) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(key != NULL && value != NULL);

  cm_hash_t h = hash(key, user);
  uintptr_t index = cm_raw_find_insert_index(map, h);

  uint8_t old_ctrl = map->ctrl[index];
  if (map->growth_left == 0 && cm_ctrl_is_empty(old_ctrl)) {
    bool success = cm_raw_reserve(map, hash, user, entry_size, 1);
    if (!success) return success;
    index = cm_raw_find_insert_index(map, h);
  }

  cm_raw_insert_at(map, h, index, entry_size, key_size, value_offset,
                   value_size, key, value);
  return true;
}

void cm_raw_drop(struct cheesemap_raw* map, uintptr_t entry_size) {
  cm_assert(map != NULL);

  if (map->ctrl == CM_CTRL_STATIC_EMPTY || map->ctrl == NULL) return;

  uint8_t* origin = cm_raw_origin(map, entry_size);
  free(origin);

  *map = cm_raw_new();
}

void cm_new(struct cheesemap* map, uintptr_t key_size, uintptr_t key_align,
            uintptr_t value_size, uintptr_t value_align, uint8_t* user,
            cm_hash_fn hash, cm_compare_fn compare) {
  cm_assert(map != NULL);
  cm_assert(hash != NULL && compare != NULL);

  uintptr_t value_offset = cm_align_up(key_size, value_align);
  uintptr_t max_align = cm_max(key_align, value_align);
  uintptr_t entry_size = cm_align_up(value_offset + value_size, max_align);

  *map = (struct cheesemap){key_size, value_size, value_offset, entry_size,
                            user,     hash,       compare,      cm_raw_new()};
}

void cm_drop(struct cheesemap* map) {
  cm_assert(map != NULL);

  cm_raw_drop(&map->raw, map->entry_size);
  memset(map, 0, sizeof(*map));
}

bool cm_insert(struct cheesemap* map, const uint8_t* key,
               const uint8_t* value) {
  cm_assert(map != NULL);
  cm_assert(key != NULL && value != NULL);

  return cm_raw_insert(&map->raw, map->hash, map->user, map->entry_size,
                       map->key_size, map->value_offset, map->value_size, key,
                       value);
}

bool cm_lookup(const struct cheesemap* map, const uint8_t* key,
               uint8_t** out_value) {
  cm_assert(map != NULL);
  cm_assert(key != NULL && out_value != NULL);

  return cm_raw_lookup(&map->raw, map->hash, map->compare, map->user,
                       map->entry_size, map->value_offset, key, out_value);
}

bool cm_remove(struct cheesemap* map, const uint8_t* key, uint8_t* out_value) {
  cm_assert(map != NULL);
  cm_assert(key != NULL);

  return cm_raw_remove(&map->raw, map->hash, map->compare, map->user,
                       map->entry_size, map->value_offset, map->value_size, key,
                       out_value);
}

bool cm_reserve(struct cheesemap* map, uintptr_t additional) {
  cm_assert(map != NULL);

  return cm_raw_reserve(&map->raw, map->hash, map->user, map->entry_size,
                        additional);
}

/* iterator */
static inline uint8_t* cm_raw_iter_next_entry(uint8_t* old_entry,
                                              uintptr_t entry_size) {
  return old_entry - entry_size * CM_GROUP_SIZE;
}

void cm_raw_iter_init(struct cheesemap_raw_iter* iter,
                      const struct cheesemap_raw* map, uintptr_t entry_size,
                      uintptr_t start_index) {
  cm_assert(map != NULL);
  cm_assert(start_index % CM_GROUP_SIZE == 0);
  memset(iter, 0, sizeof(*iter));

  uintptr_t buckets = map->bucket_mask + 1;
  cm_assert(buckets > start_index);

  uint8_t* ctrl = &map->ctrl[start_index];
  uint8_t* entry = cm_raw_elem_at(map, start_index, entry_size);

  group_t group = cm_group_load(ctrl);
  bitmask_t mask = cm_group_match_full(group);

  iter->curr_mask = mask;
  iter->curr_index = start_index;
  iter->n_ctrl = ctrl + CM_GROUP_SIZE;
  iter->n_entry = cm_raw_iter_next_entry(entry, entry_size);
  iter->end = map->ctrl + buckets;
}

static inline void cm_raw_iter_next_inner_slow(
    struct cheesemap_raw_iter* iter) {
  cm_assert(iter != NULL);

  group_t group = cm_group_load(iter->n_ctrl);
  iter->n_ctrl += CM_GROUP_SIZE;
  iter->curr_mask = cm_group_match_full(group);
  iter->curr_index += CM_GROUP_SIZE;
}

static inline uintptr_t cm_raw_iter_next_inner_fast(
    struct cheesemap_raw_iter* iter) {
  cm_assert(iter != NULL);

  uintptr_t bucket_offset = cm_bitmask_lowest_set_bit(iter->curr_mask);
  iter->curr_mask &= iter->curr_mask - 1;

  return iter->curr_index + bucket_offset;
}

bool cm_raw_iter_next(struct cheesemap_raw_iter* iter, uintptr_t entry_size,
                      uintptr_t* out_index) {
  cm_assert(iter != NULL);
  cm_assert(out_index != NULL);

  while (true) {
    if (iter->curr_mask != 0) {
      *out_index = cm_raw_iter_next_inner_fast(iter);
      return true;
    }

    if (iter->n_ctrl >= iter->end) return false;

    cm_raw_iter_next_inner_slow(iter);
    iter->n_entry = cm_raw_iter_next_entry(iter->n_entry, entry_size);
  }
}

void cm_iter_init(struct cheesemap_iter* iter, const struct cheesemap* map) {
  cm_assert(iter != NULL && map != NULL);

  iter->entry_size = map->entry_size;
  iter->value_offset = map->value_offset;
  cm_raw_iter_init(&iter->raw, &map->raw, map->entry_size, 0);
}

bool cm_iter_next(struct cheesemap_iter* iter, const struct cheesemap* map,
                  uint8_t** out_key, uint8_t** out_value) {
  cm_assert(iter != NULL && map != NULL);
  cm_assert(out_key != NULL && out_value != NULL);

  uintptr_t index;
  if (!cm_raw_iter_next(&iter->raw, iter->entry_size, &index)) return false;

  uint8_t* elem = cm_raw_elem_at(&map->raw, index, iter->entry_size);
  *out_key = elem;
  *out_value = elem + iter->value_offset;
  return true;
}
