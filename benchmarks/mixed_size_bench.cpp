// Workload 2 (README): allocate objects with sizes drawn from a log-uniform
// distribution over [8, 4096] bytes, hold a random subset live and free the
// rest, for 10M operations -- the general-case workload.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "bench_common.h"

namespace {

constexpr int kTotalOps = 10'000'000;
constexpr std::size_t kMinSize = 8;
constexpr std::size_t kMaxSize = 4096;
constexpr double kFreeProbability = 0.5;
constexpr int kSampleInterval = 100'000;

}  // namespace

int main() {
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    const double log_min = std::log(static_cast<double>(kMinSize));
    const double log_max = std::log(static_cast<double>(kMaxSize));
    auto random_size = [&] {
        double t = log_min + unit(rng) * (log_max - log_min);
        return static_cast<std::size_t>(std::exp(t));
    };

    std::vector<std::pair<void*, std::size_t>> live;
    std::size_t live_bytes = 0;
    double peak_rss = 0;

    auto start = bench::now();
    for (int i = 0; i < kTotalOps; ++i) {
        if (!live.empty() && unit(rng) < kFreeProbability) {
            std::size_t idx = rng() % live.size();
            live_bytes -= live[idx].second;
            std::free(live[idx].first);
            live[idx] = live.back();
            live.pop_back();
        } else {
            std::size_t size = random_size();
            void* p = std::malloc(size);
            live_bytes += size;
            live.emplace_back(p, size);
        }
        if (i % kSampleInterval == 0) {
            double rss = bench::peak_rss_bytes();
            if (rss > peak_rss) peak_rss = rss;
        }
    }
    auto end = bench::now();

    double secs = bench::seconds_between(start, end);
    peak_rss = std::max(peak_rss, bench::peak_rss_bytes());
    double fragmentation = peak_rss > 0 ? 1.0 - static_cast<double>(live_bytes) / peak_rss : 0.0;

    std::printf("mixed_size_bench: %d ops, sizes in [%zu, %zu]\n", kTotalOps, kMinSize, kMaxSize);
    std::printf("  wall time:    %.3f s\n", secs);
    std::printf("  throughput:   %.2f Mops/sec\n", kTotalOps / secs / 1e6);
    std::printf("  live bytes:   %.1f MB (%zu objects)\n", live_bytes / 1e6,
                 static_cast<std::size_t>(live.size()));
    std::printf("  peak RSS:     %.1f MB\n", peak_rss / 1e6);
    std::printf("  fragmentation: %.1f%%\n", fragmentation * 100.0);

    for (auto& [p, s] : live) {
        (void)s;
        std::free(p);
    }
    return 0;
}
