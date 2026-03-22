#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cheesemap.h"

struct user_info {
  const char* country;
  int age;
  int zip;
};

static const char* NAME = "Max Mustermann";
static struct user_info INFO = {
    .age = 23,
    .country = "germany",
    .zip = 69420,
};

static const char* NAME2 = "Peter Urs";
static struct user_info INFO2 = {
    .age = 64,
    .country = "switzerland",
    .zip = 1201,
};

uint64_t hash(const uint8_t* key, uint8_t* user) {
  (void)user;

  const char* name = (const char*)key;
  uint64_t count;
  while (*name) {
    count++;
    name++;
  }

  return count;
}

bool compare(const uint8_t* key1, const uint8_t* key2, uint8_t* user) {
  (void)user;

  const char* name1 = (const char*)key1;
  const char* name2 = (const char*)key2;
  return strcmp(name1, name2) == 0;
}

int main() {
  struct cheesemap map;
  cm_new(&map, sizeof(const char*), _Alignof(const char*),
         sizeof(struct user_info), _Alignof(struct user_info), NULL, hash,
         compare);

  bool ok = cm_insert(&map, (const uint8_t*)NAME, (uint8_t*)&INFO);
  if (!ok) return 1;

  ok = cm_insert(&map, (const uint8_t*)NAME2, (const uint8_t*)&INFO2);
  if (!ok) return 1;

  struct user_info* found_max;
  ok = cm_lookup(&map, (const uint8_t*)NAME, (uint8_t**)&found_max);
  if (!ok) return 1;

  if (memcmp(&INFO, found_max, sizeof(struct user_info)) != 0) return 1;
  printf("Max Mustermann is of age %d lives in %s at ZIP %d\n", found_max->age,
         found_max->country, found_max->zip);

  struct user_info* found_peter;
  ok = cm_lookup(&map, (const uint8_t*)NAME2, (uint8_t**)&found_peter);
  if (memcmp(&INFO2, found_peter, sizeof(struct user_info)) != 0) return 1;
  printf("Peter Urs is of age %d lives in %s at ZIP %d\n", found_peter->age,
         found_peter->country, found_peter->zip);

  ok = cm_remove(&map, (const uint8_t*)NAME, NULL);
  if (!ok) return 1;

  struct user_info* found_max2;
  ok = cm_lookup(&map, (const uint8_t*)NAME, (uint8_t**)&found_max2);
  if (ok) return 1;

  cm_drop(&map);
}
