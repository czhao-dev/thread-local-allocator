// Workload 3 (README): single-threaded p50/p99/p999 allocation latency for
// 64-byte objects.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "bench_common.h"

namespace {

constexpr int kIterations = 500'000;
constexpr std::size_t kObjSize = 64;

}  // namespace

int main() {
    std::vector<std::int64_t> latencies;
    latencies.reserve(kIterations);
    std::vector<void*> ptrs(kIterations);

    for (int i = 0; i < kIterations; ++i) {
        auto t0 = bench::now();
        ptrs[i] = std::malloc(kObjSize);
        auto t1 = bench::now();
        latencies.push_back(bench::nanos_between(t0, t1));
    }
    for (void* p : ptrs) std::free(p);

    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) {
        std::size_t idx = static_cast<std::size_t>(p * (latencies.size() - 1));
        return latencies[idx];
    };

    std::printf("latency_bench: %d allocations of %zuB\n", kIterations, kObjSize);
    std::printf("  p50:  %lld ns\n", static_cast<long long>(pct(0.50)));
    std::printf("  p99:  %lld ns\n", static_cast<long long>(pct(0.99)));
    std::printf("  p999: %lld ns\n", static_cast<long long>(pct(0.999)));
    return 0;
}
