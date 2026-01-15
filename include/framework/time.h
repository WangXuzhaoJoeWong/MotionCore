#pragma once

#include <chrono>
#include <thread>

#include "clock.h"

namespace wxz::framework {

/// framework 统一时间入口（便于业务侧少关心 core 细节）。
inline std::uint64_t now_epoch_ms() noexcept {
    return wxz::core::clock_now_epoch_ms();
}

inline std::chrono::steady_clock::time_point steady_now() noexcept {
    return wxz::core::clock_steady_now();
}

/// ROS2-like Rate：用于固定频率循环。
/// - 默认使用 steady clock，避免受系统时间跳变影响。
class Rate {
public:
    explicit Rate(std::chrono::milliseconds period)
        : period_(period), next_(steady_now() + period_) {}

    /// sleep 到下一周期。
    /// - 返回 false 表示 period<=0（不 sleep）。
    bool sleep() {
        if (period_.count() <= 0) return false;

        const auto now = steady_now();
        if (now < next_) {
            std::this_thread::sleep_for(next_ - now);
        }
        // 采用“追赶”模式，避免严重漂移。
        const auto after = steady_now();
        while (next_ <= after) {
            next_ += period_;
        }
        return true;
    }

private:
    std::chrono::milliseconds period_;
    std::chrono::steady_clock::time_point next_;
};

} // namespace wxz::framework
