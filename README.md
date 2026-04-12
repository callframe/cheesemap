Cheesemap
=========

Cheesemap is a Swiss-table HashMap implementation in C. It is designed to be fast, easy to understand, and easy to embed — the entire implementation is a single `.c` / `.h` pair with no dependencies.

The map is open-addressing with SIMD-accelerated control-byte matching (SSE2 / AVX2 / AVX-512 via compile-time flags). It may use slightly more memory than necessary immediately after a resize and does not yet provide a shrink method. Not yet production-tested but fully functional.

## Example

```c
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheesemap.h"

_Noreturn void panic_impl(const char* file, cm_u32 line, const char* fmt, ...) {
  fprintf(stderr, "Panic at %s:%u: ", file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  abort();
}

#define countof(arr) (sizeof(arr) / sizeof(*(arr)))

cm_u64 hash_string(const cm_u8* key, cm_u8* user) {
  (void)user;
  const char* str = *(const char**)key;
  cm_u64 hash = 5381;
  int c;
  while ((c = *str++)) hash = ((hash << 5) + hash) + c;
  return hash;
}

bool compare_string(const cm_u8* key1, const cm_u8* key2, cm_u8* user) {
  (void)user;
  return strcmp(*(const char**)key1, *(const char**)key2) == 0;
}

void* default_alloc(cm_usize size, cm_usize align, cm_u8* user) {
  (void)user;
  return aligned_alloc(align, size);
}

void default_dealloc(void* ptr, cm_u8* user) {
  (void)user;
  free(ptr);
}

int main(void) {
  struct cheesemap map;
  cm_init_(&map, const char*, int, NULL, hash_string, compare_string,
           default_alloc, default_dealloc);

  const char* words[] = {"hello", "world", "hello", "cheesemap", "world", "hello"};
  for (size_t i = 0; i < countof(words); i++) {
    int* count;
    if (cm_lookup_(&map, words[i], &count)) {
      (*count)++;
    } else {
      int initial = 1;
      cm_insert_(&map, words[i], initial);
    }
  }

  printf("Word frequencies:\n");
  struct cheesemap_iter iter;
  cm_iter_init(&iter, &map);
  const char** word;
  int* count;
  while (cm_iter_next_(&iter, &map, &word, &count)) {
    printf("  %s: %d\n", *word, *count);
  }

  const char* search = "hello";
  if (cm_lookup_(&map, search, &count))
    printf("\n'%s' appears %d times\n", search, *count);

  const char* remove = "world";
  cm_remove_(&map, remove, NULL);
  printf("Removed '%s'\n", remove);

  if (!cm_lookup_(&map, remove, &count))
    printf("'%s' no longer in map\n", remove);

  cm_drop(&map);
  return 0;
}
```

## Benchmarks

1,000,000 `u64 -> u64` entries: insert, lookup hit, lookup miss, remove. All implementations use the same murmur-inspired mixing hash where the language allows it. Best time across multiple runs is shown.

**Platform:** x86-64, Linux, performance governor.

| Implementation | Best (ms) | Notes |
|---|---|---|
| abseil flat_hash_map | ~34 | [1] |
| cheesemap SSE2 (Clang) | ~45 | |
| Rust HashMap (hashbrown) | ~48 | |
| cheesemap scalar (GCC) | ~53 | |
| cheesemap scalar (Clang) | ~56 | |
| cheesemap SSE2 (GCC) | ~58 | |
| Go map (Swiss table) | ~68 | |
| .NET Dictionary | ~70 | |
| Chromium JS Map | ~102 | |
| D associative array (LDC) | ~109 | [3] |
| std::unordered_map + mimalloc | ~110 | [2] |
| Node.js Map | ~153 | |
| Ada Hashed_Maps (GNAT) | ~147 | [4] |
| std::unordered_map | ~176 | |
| Python dict | ~193 | |

> **Note on extended L3 cache:** The test machine has a split cache topology where the benchmark's ~32 MB working set fits comfortably in one cache domain but thrashes in the other. Best times above reflect lucky scheduling onto the larger cache. Average times are roughly 1.5-2x higher. Pin with `numactl --cpunodebind=0` for reproducible results.

**Java HashMap is excluded.** `HashMap<Long, Long>` boxes every key and value into heap objects, and the JVM C2 JIT eliminates the miss-lookup loop entirely after warmup (it can prove missing keys are absent, so the loop body is dead code). The resulting numbers are not a fair comparison against native hash maps.

**[1]** abseil is the reference Swiss table implementation.  
**[2]** std::unordered_map is chained; mimalloc recovers ~38% by reducing per-node allocation cost. Without it: ~176 ms.  
**[3]** D's built-in associative array uses per-entry heap allocation; no open-addressing or SIMD.  
**[4]** Ada's `Ada.Containers.Hashed_Maps` is also chained; `Hash_Type` is 32-bit so the mixing hash is truncated, reducing distribution quality slightly.

---

Copyright © 2026 Fabrice
