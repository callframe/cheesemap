#include "common.h"

#include <unordered_map>

namespace {

struct Hasher {
    std::size_t operator()(BenchKey key) const
    {
        return static_cast<std::size_t>(BenchHashKey(key));
    }
};

class StdUnorderedMapAdapter {
public:
    void reserve(std::size_t size)
    {
        map_.reserve(size);
    }

    bool insert(BenchKey key, BenchValue value)
    {
        map_.insert_or_assign(key, value);
        return true;
    }

    bool replace(BenchKey key, BenchValue value)
    {
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        it->second = value;
        return true;
    }

    bool lookup(BenchKey key, BenchValue& value) const
    {
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        value = it->second;
        return true;
    }

    bool remove(BenchKey key)
    {
        return map_.erase(key) != 0;
    }

private:
    std::unordered_map<BenchKey, BenchValue, Hasher> map_;
};

const bool registered = [] {
    RegisterBenchmarks<StdUnorderedMapAdapter>("std_unordered_map");
    return true;
}();

} // namespace
