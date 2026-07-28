#include "cheesemap.cc"
#include "common.h"

template <>
struct cheesemap::Mapable<BenchKey>
{
  static cheesemap::Hash hash(BenchKey const& key) { return BenchHashKey(key); }
  static bool compare(BenchKey const& lhs, BenchKey const& rhs) { return lhs == rhs; }
};

template <>
struct cheesemap::Allocatable<void>
{
  static uint8_t* alloc(void* state, size_t size, size_t align)
  {
    (void)state;
    return new (std::align_val_t(align), std::nothrow) uint8_t[size];
  }

  static void dealloc(void* state, uint8_t* ptr, size_t size, size_t align)
  {
    (void)state;
    (void)size;
    operator delete(ptr, std::align_val_t(align), std::nothrow);
  }
};

namespace
{

using Map = cheesemap::Map<BenchKey, BenchValue, void>;

class CheesemapAdapter
{
 public:
  CheesemapAdapter() = default;

  ~CheesemapAdapter() { cheesemap::map_drop(&map_); }

  CheesemapAdapter(const CheesemapAdapter&) = delete;
  CheesemapAdapter& operator=(const CheesemapAdapter&) = delete;

  void reserve(std::size_t size)
  {
    if (!cheesemap::map_new_with<BenchKey, BenchValue, void>(&map_, nullptr, size))
    {
      std::abort();
    }
  }

  bool insert(BenchKey key, BenchValue value) { return cheesemap::map_insert(&map_, key, value); }

  bool replace(BenchKey key, BenchValue value) { return cheesemap::map_insert(&map_, key, value); }

  bool lookup(BenchKey key, BenchValue& value) const
  {
    BenchValue const* found = cheesemap::map_lookup(&map_, key);
    if (found == nullptr) return false;
    value = *found;
    return true;
  }

  bool remove(BenchKey key) { return cheesemap::map_remove(&map_, key); }

 private:
  Map map_ = cheesemap::map_new<BenchKey, BenchValue, void>(nullptr);
};

const bool registered = []
{
  RegisterBenchmarks<CheesemapAdapter>("cheesemap");
  return true;
}();

}  // namespace
