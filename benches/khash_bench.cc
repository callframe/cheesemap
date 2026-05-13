#include "common.h"

#include <cstdlib>

extern "C" {
#include "klib/khash.h"
}

static inline khint_t BenchKhashHash(khint64_t key)
{
    return static_cast<khint_t>(BenchHashKey(static_cast<BenchKey>(key)));
}

static inline int BenchKhashEqual(khint64_t lhs, khint64_t rhs)
{
    return lhs == rhs;
}

KHASH_INIT(bench_u64, khint64_t, BenchValue, 1, BenchKhashHash, BenchKhashEqual)

namespace {

class KhashAdapter {
public:
    KhashAdapter()
        : map_(kh_init(bench_u64))
    {
        if (map_ == nullptr) {
            std::abort();
        }
    }

    ~KhashAdapter()
    {
        kh_destroy(bench_u64, map_);
    }

    KhashAdapter(const KhashAdapter&) = delete;
    KhashAdapter& operator=(const KhashAdapter&) = delete;

    void reserve(std::size_t size)
    {
        if (kh_resize(bench_u64, map_, static_cast<khint_t>(size)) != 0) {
            std::abort();
        }
    }

    bool insert(BenchKey key, BenchValue value)
    {
        int status = 0;
        khiter_t iter = kh_put(bench_u64, map_, static_cast<khint64_t>(key), &status);
        if (status < 0) {
            return false;
        }
        kh_val(map_, iter) = value;
        return true;
    }

    bool replace(BenchKey key, BenchValue value)
    {
        khiter_t iter = kh_get(bench_u64, map_, static_cast<khint64_t>(key));
        if (iter == kh_end(map_)) {
            return false;
        }
        kh_val(map_, iter) = value;
        return true;
    }

    bool lookup(BenchKey key, BenchValue& value) const
    {
        khiter_t iter = kh_get(bench_u64, map_, static_cast<khint64_t>(key));
        if (iter == kh_end(map_)) {
            return false;
        }
        value = kh_val(map_, iter);
        return true;
    }

    bool remove(BenchKey key)
    {
        khiter_t iter = kh_get(bench_u64, map_, static_cast<khint64_t>(key));
        if (iter == kh_end(map_)) {
            return false;
        }
        kh_del(bench_u64, map_, iter);
        return true;
    }

private:
    khash_t(bench_u64) * map_ = nullptr;
};

const bool registered = [] {
    RegisterBenchmarks<KhashAdapter>("khash");
    return true;
}();

} // namespace
