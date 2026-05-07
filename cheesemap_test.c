#include "cheesemap.h"

cm_hash_t cm_hash_fn(int key)
{
    return (cm_hash_t)key;
}

bool cm_compare_fn(int a, int b)
{
    return a == b;
}

cheesemap(TestMap, int, const char*);
cheesemap_impl(TestMap, int, const char*, cm_hash_fn, cm_compare_fn);

int main(void){}