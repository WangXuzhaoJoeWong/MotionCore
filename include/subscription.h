#pragma once

#include <functional>
#include <utility>

namespace wxz::core {

// RAII 订阅令牌。
// - 仅支持移动（move-only）。
// - 析构时会调用 cancel()（除非已 detach）。
//
// 设计目标：
// 1) 支持显式取消订阅（token.reset()）。
// 2) 确保取消订阅逻辑在 core 内部（而非插件 .so 内），因此即使 dlclose 后再销毁 token 也仍然安全。
class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept : cancel_(std::move(other.cancel_)) { other.cancel_ = nullptr; }

    Subscription& operator=(Subscription&& other) noexcept {
        if (this == &other) return *this;
        reset();
        cancel_ = std::move(other.cancel_);
        other.cancel_ = nullptr;
        return *this;
    }

    ~Subscription() { reset(); }

    // 立即取消订阅。可重复调用。
    void reset() {
        if (cancel_) {
            auto fn = std::move(cancel_);
            cancel_ = nullptr;
            try {
                fn();
            } catch (...) {
                // 尽力而为：取消订阅不应导致调用方崩溃。
            }
        }
    }

    // 将 token 与底层订阅解绑；析构将不再触发取消订阅。
    // 用于保持向后兼容的 "subscribe()" 行为。
    void detach() { cancel_ = nullptr; }

    explicit operator bool() const { return static_cast<bool>(cancel_); }

private:
    friend class FastddsChannel;
    friend class InprocChannel;
    friend class ShmChannel;

    explicit Subscription(std::function<void()> cancel) : cancel_(std::move(cancel)) {}

    std::function<void()> cancel_;
};

} // namespace wxz::core
