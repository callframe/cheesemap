#include "cheesemap.h"

/* ============================================================================
 * ctrl operations
 * ========================================================================== */

/* ---- Empty control bytes -------------------------------------------------- */

const cm_u8 CM_EMPTY_CTRL[CM_GROUP_SIZE] = { [0 ... CM_GROUP_SIZE - 1] = CM_CTRL_EMPTY };

/* ---- SSE2 backend --------------------------------------------------------- */

#if defined(__SSE2__)
cm_group_t _cm_group_load(const cm_u8* ctrl)
{
    return _mm_loadu_si128((const cm_group_t*)ctrl);
}

cm_bitmask_t _cm_group_match_tag(cm_group_t group, cm_u8 tag)
{
    const __m128i tagvec = _mm_set1_epi8(tag);
    __m128i cmp = _mm_cmpeq_epi8(group, tagvec);
    // movemask packs the top bit of each byte into a 16-bit mask, giving one
    // candidate bit per ctrl byte in the loaded group.
    return _mm_movemask_epi8(cmp);
}

cm_bitmask_t _cm_group_match_empty_or_deleted(cm_group_t group)
{
    // EMPTY and DELETED both have their top bit set, so movemask directly gives
    // the "special ctrl byte" mask for the whole group.
    return _mm_movemask_epi8(group);
}

cm_bitmask_t _cm_group_match_empty(cm_group_t group)
{
    return _cm_group_match_tag(group, CM_CTRL_EMPTY);
}

cm_bitmask_t _cm_group_match_full(cm_group_t group)
{
    // FULL ctrl bytes clear the top bit, so the full-slot mask is just the
    // inverse of the special-slot mask for this 16-byte group.
    return ~_cm_group_match_empty_or_deleted(group);
}
#endif

/* ---- Scalar fallback ------------------------------------------------------ */

#if !defined(CM_NO_FALLBACK)
cm_group_t cm_group_repeat(cm_u8 v)
{
    return (cm_group_t)v * (((cm_group_t)-1) / (cm_u8)~0);
}

cm_group_t _cm_group_load(const cm_u8* ctrl)
{
    cm_assert(ctrl != NULL);

    cm_group_t v;
    memcpy(&v, ctrl, sizeof(v));
    return v;
}

cm_bitmask_t _cm_group_match_empty_or_deleted(cm_group_t group)
{
    return group & cm_group_repeat(CM_CTRL_DELETED);
}

cm_bitmask_t _cm_group_match_empty(cm_group_t group)
{
    return (group & (group << 1)) & cm_group_repeat(CM_CTRL_DELETED);
}

cm_bitmask_t _cm_group_match_full(cm_group_t group)
{
    return _cm_group_match_empty_or_deleted(group) ^ cm_group_repeat(CM_CTRL_DELETED);
}

cm_bitmask_t _cm_group_match_tag(cm_group_t group, cm_u8 tag)
{
    cm_group_t cmp = group ^ cm_group_repeat(tag);
    return (cmp - cm_group_repeat(CM_CTRL_END)) & ~cmp & cm_group_repeat(CM_CTRL_DELETED);
}
#endif

/* ============================================================================
 * Internal Methods
 * ========================================================================== */

static inline cm_usize _cm_capacity_to_buckets(cm_usize capacity)
{
    cm_usize min_buckets = (capacity * CM_LOAD_DENOM + CM_LOAD_NUM - 1) / CM_LOAD_NUM;
    cm_usize buckets = _cm_npow2(min_buckets);
    return _cm_max(buckets, CM_GROUP_SIZE);
}

cm_usize _cm_buckets_to_capacity(cm_usize bucket_mask)
{
    cm_usize num_buckets = bucket_mask + 1;
    return (num_buckets / CM_LOAD_DENOM) * CM_LOAD_NUM;
}

static inline cm_usize _cm_ctrl_offset(cm_usize buckets, cm_usize entry_size)
{
    cm_usize offset = entry_size * buckets;
    cm_usize ctrl_align = _cm_max(entry_size, CM_GROUP_SIZE);
    return _cm_alignup(offset, ctrl_align);
}

void _cm_layout(cm_usize entry_size, cm_usize entry_align, cm_usize capacity, cm_usize* out_buckets, cm_usize* out_ctrl_offset, cm_usize* out_size)
{
    cm_usize buckets = _cm_capacity_to_buckets(capacity);
    cm_usize ctrl_offset = _cm_ctrl_offset(buckets, entry_size);

    cm_usize size = ctrl_offset + buckets + CM_GROUP_SIZE;
    size = _cm_alignup(size, entry_align);

    *out_buckets = buckets;
    *out_ctrl_offset = ctrl_offset;
    *out_size = size;
}