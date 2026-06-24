#include "common.h"

#include "cheesemap.cc"

namespace {

cm_hash Hash(BenchKey key)
{
    return BenchHashKey(key);
}

bool Equal(BenchKey lhs, BenchKey rhs)
{
    return lhs == rhs;
}

cm_u8* alloc(cm_u8* ctx, cm_usize size, cm_usize align)
{
    (void)ctx;
    return new (std::align_val_t(align), std::nothrow) cm_u8[size];
}

void dealloc(cm_u8* ctx, cm_u8* ptr, cm_usize size, cm_usize align)
{
    (void)ctx;
    (void)size;
    operator delete(ptr, std::align_val_t(align), std::nothrow);
}

using Map = Cheesemap<BenchKey, BenchValue, Hash, Equal>;

class CheesemapAdapter {
public:
    CheesemapAdapter() = default;

    ~CheesemapAdapter()
    {
        cheesemap_drop(map_);
    }

    CheesemapAdapter(const CheesemapAdapter&) = delete;
    CheesemapAdapter& operator=(const CheesemapAdapter&) = delete;

    void reserve(std::size_t size)
    {
        if (!cheesemap_new_with(map_, size)) {
            std::abort();
        }
    }

    bool insert(BenchKey key, BenchValue value)
    {
        return cheesemap_insert(map_, key, value);
    }

    bool replace(BenchKey key, BenchValue value)
    {
        return cheesemap_insert(map_, key, value);
    }

    bool lookup(BenchKey key, BenchValue& value) const
    {
        return cheesemap_lookup(map_, key, value);
    }

    bool remove(BenchKey key)
    {
        return cheesemap_remove(map_, key);
    }

private:
    Cheesemap_Allocator allocator_ = { nullptr, alloc, dealloc };
    Map map_ = cheesemap_new<BenchKey, BenchValue, Hash, Equal>(&allocator_);
};

const bool registered = [] {
    RegisterBenchmarks<CheesemapAdapter>("cheesemap");
    return true;
}();

} // namespace
