#include "cheesemap.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void* cm_malloc(uintptr_t size, uint8_t* user) {
  (void)user;
  return malloc(size);
}

void cm_free(void* ptr, uint8_t* user) {
  (void)user;
  free(ptr);
}

cm_hash_t cm_hash(const uint8_t* key, uint8_t* user) {
  (void)user;
  uint64_t k = *(const uint64_t*)key;
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdULL;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53ULL;
  k ^= k >> 33;
  return k;
}

bool cm_compare(const uint8_t* key1, const uint8_t* key2, uint8_t* user) {
  (void)user;
  return *(const uint64_t*)key1 == *(const uint64_t*)key2;
}

int main() {
  struct cheesemap map;
  cm_new(&map, sizeof(uint64_t), sizeof(uint64_t),
         NULL, cm_malloc, cm_free,
         NULL, cm_hash, cm_compare);

  const uint64_t num_entries = 1000000;
  printf("Stress test: inserting %lu entries...\n", num_entries);

  for (uint64_t i = 0; i < num_entries; i++) {
    uint64_t key = i;
    uint64_t value = i * 2;
    bool success = cm_raw_insert(&map.raw, &map.fns,
                                  sizeof(uint64_t), (uint8_t*)&key,
                                  sizeof(uint64_t), (uint8_t*)&value);
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

  // Calculate memory usage
  uintptr_t entry_size = sizeof(uint64_t) + sizeof(uint64_t);
  uintptr_t buckets = map.raw.bucket_mask + 1;
  uintptr_t data_size = entry_size * buckets;
  uintptr_t ctrl_size = buckets + 8; // buckets + mirror
  uintptr_t total_size = data_size + ctrl_size;

  printf("\nMemory usage:\n");
  printf("  Entries: %lu x %lu bytes = %lu bytes\n",
         buckets, entry_size, data_size);
  printf("  Control: %lu bytes\n", ctrl_size);
  printf("  Total: %lu bytes (%.2f MB)\n",
         total_size, total_size / (1024.0 * 1024.0));
  printf("  Load factor: %.2f%% (%lu / %lu)\n",
         (map.raw.count * 100.0) / buckets, map.raw.count, buckets);
  printf("  Overhead: %.2f%% ((total - actual) / actual)\n",
         ((total_size - (num_entries * entry_size)) * 100.0) / (num_entries * entry_size));

  cm_drop(&map);
  printf("\nDone!\n");
  return 0;
}
