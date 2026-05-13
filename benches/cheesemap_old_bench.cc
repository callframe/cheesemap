#include "common.h"

#include "cheesemap.h"

#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>

extern "C" void CM_PANIC_SYM(const char* file, cm_u32 line, const char* fmt, ...)
{
    std::fprintf(stderr, "%s:%u: ", file, line);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::abort();
}

namespace {

cm_hash_t Hash(const cm_u8* key, cm_u8*)
{
    BenchKey value;
    std::memcpy(&value, key, sizeof(value));
    return BenchHashKey(value);
}

bool Equal(const cm_u8* lhs, const cm_u8* rhs, cm_u8*)
{
    BenchKey lhs_value;
    BenchKey rhs_value;
    std::memcpy(&lhs_value, lhs, sizeof(lhs_value));
    std::memcpy(&rhs_value, rhs, sizeof(rhs_value));
    return lhs_value == rhs_value;
}

cm_u8* Alloc(cm_usize size, cm_usize align, cm_u8*)
{
    void* ptr = nullptr;
    if (posix_memalign(&ptr, align, size) != 0) {
        return nullptr;
    }
    return static_cast<cm_u8*>(ptr);
}

void Dealloc(cm_u8* ptr, cm_u8*)
{
    std::free(ptr);
}

class CheesemapOldAdapter {
public:
    CheesemapOldAdapter()
    {
        cm_init(&map_, sizeof(BenchKey), alignof(BenchKey), sizeof(BenchValue), alignof(BenchValue), nullptr, Hash, Equal, Alloc, Dealloc);
    }

    ~CheesemapOldAdapter()
    {
        cm_drop(&map_);
    }

    CheesemapOldAdapter(const CheesemapOldAdapter&) = delete;
    CheesemapOldAdapter& operator=(const CheesemapOldAdapter&) = delete;

    void reserve(std::size_t size)
    {
        if (!cm_reserve(&map_, size)) {
            std::abort();
        }
    }

    bool insert(BenchKey key, BenchValue value)
    {
        return cm_insert_(&map_, key, value);
    }

    bool replace(BenchKey key, BenchValue value)
    {
        return cm_insert_(&map_, key, value);
    }

    bool lookup(BenchKey key, BenchValue& value) const
    {
        BenchValue* ptr = nullptr;
        const bool found = cm_lookup_(&map_, key, &ptr);
        if (found) {
            value = *ptr;
        }
        return found;
    }

    bool remove(BenchKey key)
    {
        return cm_remove_(&map_, key, nullptr);
    }

private:
    cheesemap map_ {};
};

const bool registered = [] {
    RegisterBenchmarks<CheesemapOldAdapter>("cheesemap_old");
    return true;
}();

} // namespace
