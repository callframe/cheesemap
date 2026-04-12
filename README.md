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

1 000 000 u64→u64 entries: insert, lookup (hit), lookup (miss), remove.
All implementations use the same murmur-inspired mixing hash where the language allows it.
Best time across multiple runs is reported — see notes on V-Cache below.

**System:** AMD Ryzen 9 7950X3D, Clang 22.1.1 / GCC 15.2.1 / Rust 1.93.0, Linux, performance governor.

```
Implementation                  Best (ms)
---------------------------------------------
abseil flat_hash_map (C++)         34        [1]
cheesemap SSE2  (Clang)            45
Java   HashMap                     ~32       [2]
Rust   HashMap (hashbrown)         48
cheesemap scalar (Clang)           56
Go     map 1.26 (Swiss table)      68
.NET   Dictionary                  70
Chromium JS Map                   102
std::unordered_map + mimalloc     ~110       [3]
cheesemap SSE2  (GCC)              58        [4]
Node.js Map                       153
std::unordered_map (Clang)        176
Python dict                       193
```

[1] abseil is the reference Swiss table implementation — its numbers are expected to be strong.  
[2] Java's best run benefits from C2 JIT reaching peak tier during the measured phase; average is ~67 ms.  
[3] std::unordered_map is a chained hash map; mimalloc recovers ~38% over the system allocator by reducing per-node allocation overhead. Without mimalloc the best observed time was ~176 ms.  
[4] GCC's codegen for this workload is ~25% slower than Clang due to a failure to inline the hot lookup path.

**Note on AMD 3D V-Cache:** The Ryzen 9 7950X3D has two CCDs — one with 96 MB L3 (3D V-Cache) and one with 32 MB L3. The benchmark's working set (~32 MB) sits right on the boundary of the smaller CCD. The OS scheduler assigns the process to a CCD at random, producing a bimodal result distribution: runs landing on the V-Cache CCD are significantly faster. Best times above reflect V-Cache-lucky runs. Average times are roughly 1.5–2× higher on this machine. To pin to the V-Cache CCD for reproducible results: `numactl --cpunodebind=0 ./bench`.

---

Copyright © 2026 Fabrice
