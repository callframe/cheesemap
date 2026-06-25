#include "cheesemap.cc"
#include "common.h"

namespace
{

cheesemap::Hash Hash(BenchKey key) { return BenchHashKey(key); }

bool Equal(BenchKey lhs, BenchKey rhs) { return lhs == rhs; }

uint8_t* alloc(uint8_t* ctx, size_t size, size_t align)
{
  (void)ctx;
  return new (std::align_val_t(align), std::nothrow) uint8_t[size];
}

void dealloc(uint8_t* ctx, uint8_t* ptr, size_t size, size_t align)
{
  (void)ctx;
  (void)size;
  operator delete(ptr, std::align_val_t(align), std::nothrow);
}

using Map = cheesemap::Map<BenchKey, BenchValue, Hash, Equal>;

class CheesemapAdapter
{
 public:
  CheesemapAdapter() = default;

  ~CheesemapAdapter() { cheesemap::map_drop(&map_, allocator_); }

  CheesemapAdapter(const CheesemapAdapter&) = delete;
  CheesemapAdapter& operator=(const CheesemapAdapter&) = delete;

  void reserve(std::size_t size)
  {
    if (!cheesemap::map_new_with(&map_, allocator_, size))
    {
      std::abort();
    }
  }

  bool insert(BenchKey key, BenchValue value)
  {
    return cheesemap::map_insert(&map_, allocator_, key, value);
  }

  bool replace(BenchKey key, BenchValue value)
  {
    return cheesemap::map_insert(&map_, allocator_, key, value);
  }

  bool lookup(BenchKey key, BenchValue& value) const
  {
    return cheesemap::map_lookup(&map_, key, &value);
  }

  bool remove(BenchKey key) { return cheesemap::map_remove(&map_, key); }

 private:
  cheesemap::IAllocator allocator_ = {nullptr, alloc, dealloc};
  Map map_ = cheesemap::map_new<BenchKey, BenchValue, Hash, Equal>();
};

const bool registered = []
{
  RegisterBenchmarks<CheesemapAdapter>("cheesemap");
  return true;
}();

}  // namespace
