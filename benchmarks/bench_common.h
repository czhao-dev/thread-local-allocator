#pragma once

#include <sys/resource.h>

#include <chrono>
#include <cstdint>

namespace bench {

using Clock = std::chrono::steady_clock;

inline Clock::time_point now() { return Clock::now(); }

inline double seconds_between(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

inline std::int64_t nanos_between(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

// Peak resident set size, in bytes, of this process so far.
inline double peak_rss_bytes() {
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss);  // bytes on macOS
#else
    return static_cast<double>(usage.ru_maxrss) * 1024.0;  // kilobytes on Linux
#endif
}

}  // namespace bench
