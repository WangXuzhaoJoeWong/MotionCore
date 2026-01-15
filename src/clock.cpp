#include "clock.h"

#include <atomic>

namespace wxz::core {

namespace {
SystemClock g_system_clock;
std::atomic<Clock*> g_clock{&g_system_clock};
}

void set_clock(Clock* c) noexcept {
    g_clock.store(c ? c : &g_system_clock, std::memory_order_release);
}

Clock& clock() noexcept {
    return *g_clock.load(std::memory_order_acquire);
}

bool has_custom_clock() noexcept {
    return g_clock.load(std::memory_order_acquire) != &g_system_clock;
}

std::uint64_t clock_now_epoch_ms() noexcept {
    using namespace std::chrono;
    const auto tp = clock().system_now();
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(tp.time_since_epoch()).count());
}

std::chrono::steady_clock::time_point clock_steady_now() noexcept {
    return clock().steady_now();
}

} // namespace wxz::core
