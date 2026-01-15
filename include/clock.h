#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace wxz::core {

// 可插拔时钟抽象：同时提供
// - system time（用于 epoch/ms、日志、RPC timestamp 等）
// - steady time（用于周期调度、timeout 等，避免受系统时间跳变影响）
class Clock {
public:
    virtual ~Clock() = default;

    virtual std::chrono::system_clock::time_point system_now() noexcept = 0;
    virtual std::chrono::steady_clock::time_point steady_now() noexcept = 0;

    // 仅用于调试/观测。
    virtual std::string_view name() const noexcept = 0;
};

class SystemClock final : public Clock {
public:
    std::chrono::system_clock::time_point system_now() noexcept override {
        return std::chrono::system_clock::now();
    }

    std::chrono::steady_clock::time_point steady_now() noexcept override {
        return std::chrono::steady_clock::now();
    }

    std::string_view name() const noexcept override { return "system"; }
};

// 进程级全局时钟。所有权由调用方保留；时钟对象的生命周期必须覆盖其注册期。
// - set_clock(nullptr) 会恢复为默认 SystemClock。
void set_clock(Clock* clock) noexcept;
Clock& clock() noexcept;

// 是否正在使用非默认时钟。
bool has_custom_clock() noexcept;

// 统一的时间读取入口（便于在已有代码里最小侵入替换）。
std::uint64_t clock_now_epoch_ms() noexcept;
std::chrono::steady_clock::time_point clock_steady_now() noexcept;

} // namespace wxz::core
