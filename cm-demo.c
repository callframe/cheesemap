#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cheesemap.h"

cm_hash_t hash_u64(const uint8_t* key, uint8_t* user) {
  (void)user;
  uint64_t k = *(const uint64_t*)key;
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdULL;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53ULL;
  k ^= k >> 33;
  return k;
}

bool compare_u64(const uint8_t* key1, const uint8_t* key2, uint8_t* user) {
  (void)user;
  return *(const uint64_t*)key1 == *(const uint64_t*)key2;
}

cm_hash_t hash_u32(const uint8_t* key, uint8_t* user) {
  (void)user;
  uint64_t k = *(const uint32_t*)key;
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdULL;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53ULL;
  k ^= k >> 33;
  return k;
}

bool compare_u32(const uint8_t* key1, const uint8_t* key2, uint8_t* user) {
  (void)user;
  return *(const uint32_t*)key1 == *(const uint32_t*)key2;
}

int main() {
  printf("=== Alignment test (uint32 key, uint64 value) ===\n");
  {
    struct cheesemap amap;
    cm_new(&amap, sizeof(uint32_t), _Alignof(uint32_t), sizeof(uint64_t),
           _Alignof(uint64_t), NULL, hash_u32, compare_u32);

    printf("  key_size=%lu, value_size=%lu\n", amap.key_size, amap.value_size);
    printf("  value_offset=%lu (expected 8, padding=4)\n", amap.value_offset);
    printf("  entry_size=%lu (expected 16)\n", amap.entry_size);

    if (amap.value_offset != 8 || amap.entry_size != 16) {
      printf("  ERROR: alignment calculation wrong!\n");
      return 1;
    }

    for (uint32_t i = 0; i < 1000; i++) {
      uint32_t key = i;
      uint64_t value = (uint64_t)i * 0x100000001ULL;
      cm_insert(&amap, (uint8_t*)&key, (uint8_t*)&value);
    }

    uint64_t errors = 0;
    for (uint32_t i = 0; i < 1000; i++) {
      uint32_t key = i;
      uint8_t* value_ptr;
      if (cm_lookup(&amap, (uint8_t*)&key, &value_ptr)) {
        uint64_t value = *(uint64_t*)value_ptr;
        uint64_t expected = (uint64_t)i * 0x100000001ULL;
        if (value != expected) {
          printf("  ERROR: key=%u value=%lu expected=%lu\n", i, value,
                 expected);
          errors++;
        }
      } else {
        printf("  ERROR: key=%u not found\n", i);
        errors++;
      }
    }

    if (errors == 0) {
      printf("  OK: all 1000 entries verified with correct alignment\n");
    }

    cm_drop(&amap);
  }
  printf("\n");

  struct cheesemap map;
  cm_new(&map, sizeof(uint64_t), _Alignof(uint64_t), sizeof(uint64_t),
         _Alignof(uint64_t), NULL, hash_u64, compare_u64);

  const uint64_t num_entries = 1000000;
  printf("Stress test: inserting %lu entries...\n", num_entries);

  for (uint64_t i = 0; i < num_entries; i++) {
    uint64_t key = i;
    uint64_t value = i * 2;
    bool success = cm_insert(&map, (uint8_t*)&key, (uint8_t*)&value);
    if (!success) {
      printf("Insert failed at i=%lu\n", i);
      cm_drop(&map);
      return 1;
    }

    if ((i + 1) % 100000 == 0) {
      printf("  Progress: %lu entries, %lu buckets, %lu growth_left\n",
             map.raw.count, map.raw.bucket_mask + 1, map.raw.growth_left);
    }
  }

  printf("\nSuccessfully inserted %lu entries!\n", num_entries);
  printf("Map count: %lu\n", map.raw.count);
  printf("Map buckets: %lu\n", map.raw.bucket_mask + 1);
  printf("Growth left: %lu\n", map.raw.growth_left);

  // Test lookups
  printf("\nTesting lookups...\n");
  for (uint64_t i = 0; i < 10; i++) {
    uint64_t key = i * 100000;
    uint8_t* value_ptr;
    if (cm_lookup(&map, (uint8_t*)&key, &value_ptr)) {
      uint64_t value = *(uint64_t*)value_ptr;
      printf("  Lookup key=%lu -> value=%lu (expected %lu)\n", key, value,
             key * 2);
    } else {
      printf("  Lookup key=%lu FAILED\n", key);
    }
  }

  // Test iteration
  printf("\nIterating first 10 entries...\n");
  struct cheesemap_iter iter;
  cm_iter_init(&iter, &map);
  uintptr_t count = 0;
  uint8_t *key_ptr, *value_ptr;
  while (cm_iter_next(&iter, &map, &key_ptr, &value_ptr)) {
    uint64_t key = *(uint64_t*)key_ptr;
    uint64_t value = *(uint64_t*)value_ptr;
    if (count < 10) {
      printf("  [%lu] key=%lu, value=%lu\n", count, key, value);
    }
    count++;
  }
  printf("  Total iterated: %lu entries\n", count);

  // Test removes
  printf("\nTesting removes...\n");
  for (uint64_t i = 0; i < 5; i++) {
    uint64_t key = i * 200000;
    uint64_t old_value;
    if (cm_remove(&map, (uint8_t*)&key, (uint8_t*)&old_value)) {
      printf("  Removed key=%lu, old_value=%lu\n", key, old_value);
    } else {
      printf("  Remove key=%lu FAILED\n", key);
    }
  }
  printf("Map count after removes: %lu\n", map.raw.count);

  // Verify removes worked
  printf("\nVerifying removes...\n");
  for (uint64_t i = 0; i < 5; i++) {
    uint64_t key = i * 200000;
    uint8_t* value_ptr;
    if (cm_lookup(&map, (uint8_t*)&key, &value_ptr)) {
      printf("  ERROR: key=%lu still exists!\n", key);
    } else {
      printf("  Confirmed key=%lu removed\n", key);
    }
  }

  // Deep check: verify ALL remaining entries
  printf("\nDeep check: verifying all %lu remaining entries...\n",
         map.raw.count);
  uint64_t checked = 0, errors = 0;
  for (uint64_t i = 0; i < num_entries; i++) {
    uint64_t key = i;
    uint8_t* value_ptr;
    bool should_exist = (i % 200000 != 0 || i / 200000 >= 5);

    if (cm_lookup(&map, (uint8_t*)&key, &value_ptr)) {
      uint64_t value = *(uint64_t*)value_ptr;
      if (!should_exist) {
        printf("  ERROR: key=%lu exists but should be removed!\n", key);
        errors++;
      } else if (value != key * 2) {
        printf("  ERROR: key=%lu has wrong value %lu (expected %lu)\n", key,
               value, key * 2);
        errors++;
      }
      checked++;
    } else {
      if (should_exist) {
        printf("  ERROR: key=%lu missing but should exist!\n", key);
        errors++;
      }
    }
  }
  printf("  Checked %lu entries, found %lu errors\n", checked, errors);

  // Verify iteration count matches
  printf("\nVerifying iteration count...\n");
  cm_iter_init(&iter, &map);
  count = 0;
  while (cm_iter_next(&iter, &map, &key_ptr, &value_ptr)) {
    count++;
  }
  if (count == map.raw.count) {
    printf("  OK: iteration count %lu matches map count\n", count);
  } else {
    printf("  ERROR: iteration count %lu != map count %lu\n", count,
           map.raw.count);
  }

  // Calculate memory usage
  uintptr_t entry_size = map.entry_size;
  uintptr_t buckets = map.raw.bucket_mask + 1;
  uintptr_t data_size = entry_size * buckets;
  uintptr_t ctrl_size = buckets + 8;  // buckets + mirror
  uintptr_t total_size = data_size + ctrl_size;

  printf("\nMemory usage:\n");
  printf("  Entries: %lu x %lu bytes = %lu bytes\n", buckets, entry_size,
         data_size);
  printf("  Control: %lu bytes\n", ctrl_size);
  printf("  Total: %lu bytes (%.2f MB)\n", total_size,
         total_size / (1024.0 * 1024.0));
  printf("  Load factor: %.2f%% (%lu / %lu)\n",
         (map.raw.count * 100.0) / buckets, map.raw.count, buckets);
  printf("  Overhead: %.2f%% ((total - actual) / actual)\n",
         ((total_size - (num_entries * entry_size)) * 100.0) /
             (num_entries * entry_size));

  cm_drop(&map);
  printf("\nDone!\n");
  return 0;
}
