#include "cheesemap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CM_ATTR(...) __attribute__((__VA_ARGS__))

CM_ATTR(hot) static inline cm_usize cm_ctz(cm_usize val) {
  cm_assert(val != 0);
#if UINTPTR_MAX == UINT64_MAX
  return (cm_usize)__builtin_ctzll(val);
#elif UINTPTR_MAX == UINT32_MAX
  return (cm_usize)__builtin_ctz(val);
#else
#error "unknown word width"
#endif
}

CM_ATTR(hot) static inline cm_usize cm_clz(cm_usize val) {
  cm_assert(val != 0);
#if UINTPTR_MAX == UINT64_MAX
  return (cm_usize)__builtin_clzll(val);
#elif UINTPTR_MAX == UINT32_MAX
  return (cm_usize)__builtin_clz(val);
#else
#error "unknown word width"
#endif
}

CM_ATTR(hot) static inline cm_usize cm_bitmask_lowest_set_bit(bitmask_t mask) {
#if CM_GROUP_SIZE == 8
  return cm_ctz(mask) / CHAR_BIT;
#elif CM_GROUP_SIZE == 16
  return cm_ctz(mask);
#else
#error "unknown group size"
#endif
}

CM_ATTR(hot) static inline cm_usize cm_bitmask_ctz(bitmask_t mask) {
  if (mask == 0) return CM_GROUP_SIZE;
#if CM_GROUP_SIZE == 8
  return cm_ctz(mask) / CHAR_BIT;
#elif CM_GROUP_SIZE == 16
  return cm_ctz(mask);
#else
#error "unknown group size"
#endif
}

CM_ATTR(hot) static inline cm_usize cm_bitmask_clz(bitmask_t mask) {
  if (mask == 0) return CM_GROUP_SIZE;
#if CM_GROUP_SIZE == 8
  return cm_clz(mask) / CHAR_BIT;
#elif CM_GROUP_SIZE == 16
  return cm_clz(mask);
#else
#error "unknown group size"
#endif
}

#define cm_max(x, y) ((x) > (y) ? (x) : (y))
#define cm_ispow2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

static inline cm_usize cm_align_up(cm_usize value, cm_usize alignment) {
  cm_assert(cm_ispow2(alignment));
  return (value + alignment - 1) & ~(alignment - 1);
}

static inline cm_usize cm_npow2(cm_usize v) {
  if (v <= 1) return 1;
  return (cm_usize)1 << (CM_WORD_WIDTH - cm_clz(v - 1));
}

struct sequence {
  cm_usize pos;
  cm_usize stride;
};

#define sequence_init(pos, stride) ((struct sequence){pos, stride})

static inline void cm_sequence_next(struct sequence* sequence,
                                    cm_usize bucket_mask) {
  cm_assert(sequence != NULL);
  cm_assert(sequence->stride <= bucket_mask);

  sequence->stride += CM_GROUP_SIZE;
  sequence->pos += sequence->stride;
  sequence->pos &= bucket_mask;
}

/* ctrl ops */
static inline bool cm_ctrl_is_special(cm_u8 v) { return v & CM_CTRL_DELETED; }

static inline bool cm_ctrl_is_empty(cm_u8 v) {
  cm_assert(cm_ctrl_is_special(v) == true);
  return (v & CM_CTRL_END) != 0;
}

/* group ops */
static inline group_t cm_group_load(const cm_u8* ctrl);
static inline bitmask_t cm_group_match_tag(group_t group, cm_u8 tag);
static inline bitmask_t cm_group_match_empty_or_deleted(group_t group);
static inline bitmask_t cm_group_match_empty(group_t group);
static inline bitmask_t cm_group_match_full(group_t group);

/* sse2 implementation */
#ifdef CM_ENABLE_SSE2
static inline group_t cm_group_load(const cm_u8* ctrl) {
  cm_assert(ctrl != NULL);
  return _mm_loadu_si128((const group_t*)ctrl);
}

static inline bitmask_t cm_group_match_tag(group_t group, cm_u8 tag) {
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
static inline group_t cm_group_repeat(cm_u8 v) {
  return (group_t)v * (((group_t)-1) / (cm_u8)~0);
}

static inline group_t cm_group_load(const cm_u8* ctrl) {
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

static inline bitmask_t cm_group_match_tag(group_t group, cm_u8 tag) {
  group_t cmp = group ^ cm_group_repeat(tag);
  return (cmp - cm_group_repeat(CM_CTRL_END)) & ~cmp &
         cm_group_repeat(CM_CTRL_DELETED);
}
#endif

/* ctrl's n stuff */

static inline cm_usize cm_h1(cm_hash_t hash) {
  return (cm_usize)(hash >> CM_FP_SIZE);
}

static inline cm_u8 cm_h2(cm_hash_t hash) {
  cm_usize top = hash >> (sizeof(cm_hash_t) * CHAR_BIT - CM_FP_SIZE);
  return (cm_u8)(top & CM_H2_MASK);
}

const cm_u8 CM_CTRL_STATIC_EMPTY[CM_GROUP_SIZE] = {[0 ... CM_GROUP_SIZE - 1] =
                                                       CM_CTRL_EMPTY};

/* hashmap implementation */
static inline cm_usize cm_buckets_to_capacity(cm_usize bucket_mask) {
  cm_usize num_buckets = bucket_mask + 1;
  return (num_buckets / CM_LOAD_DENOM) * CM_LOAD_NUM;
}

static inline cm_usize cm_capacity_to_buckets(cm_usize capacity) {
  cm_usize min_buckets =
      (capacity * CM_LOAD_DENOM + CM_LOAD_NUM - 1) / CM_LOAD_NUM;
  cm_usize buckets = cm_npow2(min_buckets);
  return cm_max(buckets, CM_GROUP_SIZE);
}

static inline cm_usize cm_alloc_align(const struct cm_type* type) {
  return cm_max(type->entry_align, _Alignof(max_align_t));
}

static inline cm_usize cm_ctrl_offset(cm_usize buckets,
                                      const struct cm_type* type) {
  cm_usize offset = type->entry_size * buckets;
  cm_usize ctrl_align = cm_max(type->entry_align, CM_GROUP_SIZE);
  return cm_align_up(offset, ctrl_align);
}

static inline void cm_raw_layout(const struct cm_type* type, cm_usize capacity,
                                 cm_usize* out_buckets,
                                 cm_usize* out_ctrl_offset,
                                 cm_usize* out_size) {
  cm_assert(type != NULL && out_buckets != NULL);
  cm_assert(out_ctrl_offset != NULL && out_size != NULL);

  cm_usize buckets = cm_capacity_to_buckets(capacity);
  cm_usize ctrl_offset = cm_ctrl_offset(buckets, type);

  cm_usize size = ctrl_offset + buckets + CM_GROUP_SIZE;
  size = cm_align_up(size, cm_alloc_align(type));

  *out_buckets = buckets;
  *out_ctrl_offset = ctrl_offset;
  *out_size = size;
}

static inline cm_u8* cm_raw_elem_at(const struct cheesemap_raw* map,
                                    cm_usize index,
                                    const struct cm_type* type) {
  cm_assert(map != NULL);
  cm_assert(map->bucket_mask + 1 > index);

  return map->ctrl - type->entry_size * (index + 1);
}

static inline cm_u8* cm_raw_origin(const struct cheesemap_raw* map,
                                   const struct cm_type* type) {
  cm_assert(map != NULL);
  cm_usize buckets = map->bucket_mask + 1;
  cm_usize ctrl_offset = cm_ctrl_offset(buckets, type);
  return map->ctrl - ctrl_offset;
}

static inline void cm_raw_ctrl_set(struct cheesemap_raw* map, cm_usize index,
                                   cm_u8 ctrl) {
  cm_assert(map != NULL);

  cm_usize index2 =
      ((index - CM_GROUP_SIZE) & map->bucket_mask) + CM_GROUP_SIZE;
  map->ctrl[index] = ctrl;
  map->ctrl[index2] = ctrl;
}

static bool cm_raw_find_insert_index_in_group(const struct cheesemap_raw* map,
                                              const group_t* group,
                                              const struct sequence* seq,
                                              cm_usize* out_index) {
  cm_assert(map != NULL);
  cm_assert(group != NULL && seq != NULL);
  cm_assert(out_index != NULL);

  bitmask_t mask = cm_group_match_empty_or_deleted(*group);
  if (mask == 0) return false;

  cm_usize bucket_offset = cm_bitmask_lowest_set_bit(mask);
  *out_index = (seq->pos + bucket_offset) & map->bucket_mask;
  return true;
}

static cm_usize cm_raw_find_insert_index(const struct cheesemap_raw* map,
                                         cm_hash_t hash) {
  cm_assert(map != NULL);

  struct sequence seq = sequence_init(cm_h1(hash) & map->bucket_mask, 0);
  while (true) {
    cm_u8* ctrl = &map->ctrl[seq.pos];
    group_t group = cm_group_load(ctrl);

    cm_usize index;
    if (cm_raw_find_insert_index_in_group(map, &group, &seq, &index))
      return index;
    cm_sequence_next(&seq, map->bucket_mask);
  }
}

static bool cm_raw_find_in_group(const struct cheesemap_raw* map,
                                 cm_compare_fn compare, cm_u8* user,
                                 group_t group, const struct sequence* seq,
                                 cm_u8 h2, const struct cm_type* type,
                                 const cm_u8* key, cm_usize* out_index) {
  cm_assert(map != NULL && compare != NULL);
  cm_assert(seq != NULL);
  cm_assert(key != NULL && out_index != NULL);

  bitmask_t mask = cm_group_match_tag(group, h2);
  while (mask != 0) {
    cm_usize bucket_offset = cm_bitmask_lowest_set_bit(mask);
    cm_usize index = (seq->pos + bucket_offset) & map->bucket_mask;

    cm_u8* elem = cm_raw_elem_at(map, index, type);
    if (compare(key, elem, user)) {
      *out_index = index;
      return true;
    }

    mask &= mask - 1;
  }

  return false;
}

static bool cm_raw_find(const struct cheesemap_raw* map, cm_compare_fn compare,
                        cm_u8* user, cm_hash_t hash, const struct cm_type* type,
                        const cm_u8* key, cm_usize* out_index) {
  cm_assert(map != NULL && compare != NULL);
  cm_assert(key != NULL && out_index != NULL);

  cm_u8 h2 = cm_h2(hash);
  struct sequence seq = sequence_init(cm_h1(hash) & map->bucket_mask, 0);

  while (true) {
    cm_u8* ctrl = &map->ctrl[seq.pos];
    group_t group = cm_group_load(ctrl);

    if (cm_raw_find_in_group(map, compare, user, group, &seq, h2, type, key,
                             out_index))
      return true;

    if (cm_group_match_empty(group) != 0) return false;

    cm_sequence_next(&seq, map->bucket_mask);
  }
}

static void cm_raw_insert_at(struct cheesemap_raw* map, cm_hash_t hash,
                             cm_usize index, const struct cm_type* type,
                             const cm_u8* key, const cm_u8* value) {
  cm_assert(map != NULL);
  cm_assert(value != NULL);

  cm_u8 old_ctrl = map->ctrl[index];
  map->growth_left -= cm_ctrl_is_empty(old_ctrl);
  cm_raw_ctrl_set(map, index, cm_h2(hash));

  cm_u8* elem = cm_raw_elem_at(map, index, type);
  memcpy(elem, key, type->key_size);
  memcpy(elem + type->value_offset, value, type->value_size);

  map->count += 1;
}

static void cm_raw_rehash(struct cheesemap_raw* old_map,
                          struct cheesemap_raw* new_map, cm_hash_fn hash,
                          cm_u8* user, const struct cm_type* type) {
  cm_assert(old_map != NULL);
  cm_assert(new_map != NULL && hash != NULL);

  struct cheesemap_raw_iter iter;
  cm_raw_iter_init(&iter, old_map, type, 0);

  cm_usize index;
  while (cm_raw_iter_next(&iter, type, &index)) {
    cm_u8* elem = cm_raw_elem_at(old_map, index, type);
    cm_hash_t h = hash(elem, user);

    cm_usize new_index = cm_raw_find_insert_index(new_map, h);
    cm_raw_ctrl_set(new_map, new_index, cm_h2(h));

    cm_u8* new_elem = cm_raw_elem_at(new_map, new_index, type);
    memcpy(new_elem, elem, type->entry_size);
  }

  new_map->count = old_map->count;
  new_map->growth_left =
      cm_buckets_to_capacity(new_map->bucket_mask) - new_map->count;
}

static bool cm_raw_resize(struct cheesemap_raw* map, cm_hash_fn hash,
                          cm_alloc_fn alloc, cm_dealloc_fn dealloc, cm_u8* user,
                          const struct cm_type* type, cm_usize new_capacity) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(alloc != NULL && dealloc != NULL);

  struct cheesemap_raw new_map;
  bool success = cm_raw_init_with(&new_map, alloc, user, type, new_capacity);
  if (!success) return false;

  cm_raw_rehash(map, &new_map, hash, user, type);

  cm_raw_drop(map, dealloc, user, type);
  *map = new_map;
  return true;
}

bool cm_raw_init_with(struct cheesemap_raw* map, cm_alloc_fn alloc, cm_u8* user,
                      const struct cm_type* type, cm_usize init_cap) {
  cm_assert(map != NULL);
  cm_assert(alloc != NULL);
  memset(map, 0, sizeof(*map));

  cm_usize buckets, ctrl_offset, size;
  cm_raw_layout(type, init_cap, &buckets, &ctrl_offset, &size);

  cm_u8* ptr = alloc(size, cm_alloc_align(type), user);
  if (ptr == NULL) return false;

  cm_u8* ctrl = ptr + ctrl_offset;
  memset(ctrl, CM_CTRL_EMPTY, buckets);
  memcpy(ctrl + buckets, ctrl, CM_GROUP_SIZE);

  map->bucket_mask = buckets - 1;
  map->growth_left = cm_buckets_to_capacity(map->bucket_mask);
  map->ctrl = ctrl;

  return true;
}

bool cm_raw_reserve(struct cheesemap_raw* map, cm_hash_fn hash,
                    cm_alloc_fn alloc, cm_dealloc_fn dealloc, cm_u8* user,
                    const struct cm_type* type, cm_usize additional) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(alloc != NULL && dealloc != NULL);

  if (map->growth_left >= additional) return true;
  // TODO: inplace rehash
  cm_usize needed = map->count + additional;
  cm_usize capacity = cm_buckets_to_capacity(map->bucket_mask);
  cm_usize new_capacity = cm_max(needed, capacity + 1);
  return cm_raw_resize(map, hash, alloc, dealloc, user, type, new_capacity);
}

bool cm_raw_lookup(const struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, cm_u8* user,
                   const struct cm_type* type, const cm_u8* key,
                   cm_u8** out_value) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(key != NULL && out_value != NULL);

  cm_hash_t h = hash(key, user);
  cm_usize index;

  if (!cm_raw_find(map, compare, user, h, type, key, &index)) return false;

  cm_u8* elem = cm_raw_elem_at(map, index, type);
  *out_value = elem + type->value_offset;
  return true;
}

bool cm_raw_remove(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_compare_fn compare, cm_u8* user,
                   const struct cm_type* type, const cm_u8* key,
                   cm_u8* out_value) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(key != NULL);

  cm_hash_t h = hash(key, user);
  cm_usize index;

  if (!cm_raw_find(map, compare, user, h, type, key, &index)) return false;

  if (out_value != NULL) {
    cm_u8* elem = cm_raw_elem_at(map, index, type);
    memcpy(out_value, elem + type->value_offset, type->value_size);
  }

  cm_usize index_before = (index - CM_GROUP_SIZE) & map->bucket_mask;
  group_t group_before = cm_group_load(&map->ctrl[index_before]);
  group_t group_at = cm_group_load(&map->ctrl[index]);

  bitmask_t empty_before = cm_group_match_empty(group_before);
  bitmask_t empty_after = cm_group_match_empty(group_at);

  cm_usize empty_count =
      cm_bitmask_clz(empty_before) + cm_bitmask_ctz(empty_after);
  cm_u8 ctrl = (empty_count >= CM_GROUP_SIZE) ? CM_CTRL_DELETED : CM_CTRL_EMPTY;

  if (ctrl == CM_CTRL_EMPTY) map->growth_left += 1;

  cm_raw_ctrl_set(map, index, ctrl);
  map->count -= 1;

  return true;
}

bool cm_raw_insert(struct cheesemap_raw* map, cm_hash_fn hash,
                   cm_alloc_fn alloc, cm_dealloc_fn dealloc, cm_u8* user,
                   const struct cm_type* type, const cm_u8* key,
                   const cm_u8* value) {
  cm_assert(map != NULL && hash != NULL);
  cm_assert(alloc != NULL && dealloc != NULL);
  cm_assert(key != NULL && value != NULL);

  cm_hash_t h = hash(key, user);
  cm_usize index = cm_raw_find_insert_index(map, h);

  cm_u8 old_ctrl = map->ctrl[index];
  if (map->growth_left == 0 && cm_ctrl_is_empty(old_ctrl)) {
    bool success = cm_raw_reserve(map, hash, alloc, dealloc, user, type, 1);
    if (!success) return success;
    index = cm_raw_find_insert_index(map, h);
  }

  cm_raw_insert_at(map, h, index, type, key, value);
  return true;
}

void cm_raw_drop(struct cheesemap_raw* map, cm_dealloc_fn dealloc, cm_u8* user,
                 const struct cm_type* type) {
  cm_assert(map != NULL);
  cm_assert(dealloc != NULL);

  if (map->ctrl == CM_CTRL_STATIC_EMPTY || map->ctrl == NULL) return;

  cm_u8* origin = cm_raw_origin(map, type);
  dealloc(origin, user);

  *map = cm_raw_new();
}

void cm_init(struct cheesemap* map, cm_usize key_size, cm_usize key_align,
             cm_usize value_size, cm_usize value_align, cm_u8* user,
             cm_hash_fn hash, cm_compare_fn compare, cm_alloc_fn alloc,
             cm_dealloc_fn dealloc) {
  cm_assert(map != NULL);
  cm_assert(hash != NULL && compare != NULL);
  cm_assert(alloc != NULL && dealloc != NULL);

  cm_usize value_offset = cm_align_up(key_size, value_align);
  cm_usize entry_align = cm_max(key_align, value_align);
  cm_usize entry_size = cm_align_up(value_offset + value_size, entry_align);

  struct cm_type type =
      cm_type_new(key_size, value_size, value_offset, entry_size, entry_align);
  *map = cm_init_inner(type, user, hash, compare, alloc, dealloc, cm_raw_new());
}

void cm_drop(struct cheesemap* map) {
  cm_assert(map != NULL);

  cm_raw_drop(&map->raw, map->dealloc, map->user, &map->type);
  memset(map, 0, sizeof(*map));
}

bool cm_insert(struct cheesemap* map, const cm_u8* key, const cm_u8* value) {
  cm_assert(map != NULL);
  cm_assert(key != NULL && value != NULL);

  return cm_raw_insert(&map->raw, map->hash, map->alloc, map->dealloc,
                       map->user, &map->type, key, value);
}

bool cm_lookup(const struct cheesemap* map, const cm_u8* key,
               cm_u8** out_value) {
  cm_assert(map != NULL);
  cm_assert(key != NULL && out_value != NULL);

  return cm_raw_lookup(&map->raw, map->hash, map->compare, map->user,
                       &map->type, key, out_value);
}

bool cm_remove(struct cheesemap* map, const cm_u8* key, cm_u8* out_value) {
  cm_assert(map != NULL);
  cm_assert(key != NULL);

  return cm_raw_remove(&map->raw, map->hash, map->compare, map->user,
                       &map->type, key, out_value);
}

bool cm_reserve(struct cheesemap* map, cm_usize additional) {
  cm_assert(map != NULL);

  return cm_raw_reserve(&map->raw, map->hash, map->alloc, map->dealloc,
                        map->user, &map->type, additional);
}

/* iterator */
static inline cm_u8* cm_raw_iter_next_entry(cm_u8* old_entry,
                                            const struct cm_type* type) {
  return old_entry - type->entry_size * CM_GROUP_SIZE;
}

void cm_raw_iter_init(struct cheesemap_raw_iter* iter,
                      const struct cheesemap_raw* map,
                      const struct cm_type* type, cm_usize start_index) {
  cm_assert(map != NULL);
  cm_assert(start_index % CM_GROUP_SIZE == 0);
  memset(iter, 0, sizeof(*iter));

  cm_usize buckets = map->bucket_mask + 1;
  cm_assert(buckets > start_index);

  cm_u8* ctrl = &map->ctrl[start_index];
  cm_u8* entry = cm_raw_elem_at(map, start_index, type);

  group_t group = cm_group_load(ctrl);
  bitmask_t mask = cm_group_match_full(group);

  iter->curr_mask = mask;
  iter->curr_index = start_index;
  iter->n_ctrl = ctrl + CM_GROUP_SIZE;
  iter->n_entry = cm_raw_iter_next_entry(entry, type);
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

static inline cm_usize cm_raw_iter_next_inner_fast(
    struct cheesemap_raw_iter* iter) {
  cm_assert(iter != NULL);

  cm_usize bucket_offset = cm_bitmask_lowest_set_bit(iter->curr_mask);
  iter->curr_mask &= iter->curr_mask - 1;

  return iter->curr_index + bucket_offset;
}

bool cm_raw_iter_next(struct cheesemap_raw_iter* iter,
                      const struct cm_type* type, cm_usize* out_index) {
  cm_assert(iter != NULL);
  cm_assert(out_index != NULL);

  while (true) {
    if (iter->curr_mask != 0) {
      *out_index = cm_raw_iter_next_inner_fast(iter);
      return true;
    }

    if (iter->n_ctrl >= iter->end) return false;

    cm_raw_iter_next_inner_slow(iter);
    iter->n_entry = cm_raw_iter_next_entry(iter->n_entry, type);
  }
}

void cm_iter_init(struct cheesemap_iter* iter, const struct cheesemap* map) {
  cm_assert(iter != NULL && map != NULL);

  iter->entry_size = map->type.entry_size;
  iter->value_offset = map->type.value_offset;
  cm_raw_iter_init(&iter->raw, &map->raw, &map->type, 0);
}

bool cm_iter_next(struct cheesemap_iter* iter, const struct cheesemap* map,
                  cm_u8** out_key, cm_u8** out_value) {
  cm_assert(iter != NULL && map != NULL);
  cm_assert(out_key != NULL && out_value != NULL);

  cm_usize index;
  if (!cm_raw_iter_next(&iter->raw, &map->type, &index)) return false;

  cm_u8* elem = cm_raw_elem_at(&map->raw, index, &map->type);
  *out_key = elem;
  *out_value = elem + iter->value_offset;
  return true;
}
