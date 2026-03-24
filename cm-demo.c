#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheesemap.h"

_Noreturn void panic_impl(const char* file, uint32_t line, const char* fmt,
                          ...) {
  fprintf(stderr, "Panic at %s:%u: ", file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  abort();
}

// Convenience macro for array length
#define countof(arr) (sizeof(arr) / sizeof(*(arr)))

// Simple hash function for string keys
uint64_t hash_string(const uint8_t* key, uint8_t* user) {
  (void)user;
  const char* str = *(const char**)key;
  uint64_t hash = 5381;
  int c;
  while ((c = *str++)) hash = ((hash << 5) + hash) + c;  // hash * 33 + c
  return hash;
}

// Compare function for string keys
bool compare_string(const uint8_t* key1, const uint8_t* key2, uint8_t* user) {
  (void)user;
  return strcmp(*(const char**)key1, *(const char**)key2) == 0;
}

// Default allocator (uses malloc)
void* default_alloc(uintptr_t size, uint8_t* user) {
  (void)user;
  return malloc(size);
}

// Default deallocator (uses free)
void default_dealloc(void* ptr, uint8_t* user) {
  (void)user;
  free(ptr);
}

int main(void) {
  // Create a map: string -> int (word frequency counter)
  struct cheesemap map;
  cm_new_(&map, const char*, int, NULL, hash_string, compare_string,
          default_alloc, default_dealloc);

  // Count word frequencies
  const char* words[] = {"hello",     "world", "hello",
                         "cheesemap", "world", "hello"};
  for (size_t i = 0; i < countof(words); i++) {
    int* count;
    if (cm_lookup_(&map, words[i], &count)) {
      (*count)++;  // Word exists, increment
    } else {
      int initial = 1;
      cm_insert_(&map, words[i], initial);
    }
  }

  // Iterate and print all word counts
  printf("Word frequencies:\n");
  struct cheesemap_iter iter;
  cm_iter_init(&iter, &map);
  const char** word;
  int* count;
  while (cm_iter_next_(&iter, &map, &word, &count)) {
    printf("  %s: %d\n", *word, *count);
  }

  // Lookup a specific word
  const char* search = "hello";
  if (cm_lookup_(&map, search, &count)) {
    printf("\n'%s' appears %d times\n", search, *count);
  }

  // Remove a word
  const char* remove = "world";
  cm_remove_(&map, remove, NULL);
  printf("Removed '%s'\n", remove);

  // Verify removal
  if (!cm_lookup_(&map, remove, &count)) {
    printf("'%s' no longer in map\n", remove);
  }

  cm_drop(&map);
  return 0;
}
