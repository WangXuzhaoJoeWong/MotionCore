#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace wxz::core {

class ByteBufferPool;

// 可复用字节缓冲区的 move-only 租约（lease）。
// lease 析构时会把 buffer 归还到所属的 pool。
class ByteBufferLease {
public:
    ByteBufferLease() = default;
    ByteBufferLease(const ByteBufferLease&) = delete;
    ByteBufferLease& operator=(const ByteBufferLease&) = delete;

    ByteBufferLease(ByteBufferLease&& other) noexcept { *this = std::move(other); }
    ByteBufferLease& operator=(ByteBufferLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        state_ = std::move(other.state_);
        buf_ = other.buf_;
        other.buf_ = nullptr;
        size_ = other.size_;
        other.size_ = 0;
        return *this;
    }

    ~ByteBufferLease() { reset(); }

    explicit operator bool() const { return buf_ != nullptr; }

    std::uint8_t* data() { return buf_ ? buf_->data.data() : nullptr; }
    const std::uint8_t* data() const { return buf_ ? buf_->data.data() : nullptr; }
    std::size_t capacity() const { return buf_ ? buf_->data.size() : 0; }

    std::size_t size() const { return size_; }
    void set_size(std::size_t n) { size_ = n; }

private:
    friend class ByteBufferPool;

    struct Buffer {
        std::vector<std::uint8_t> data;
    };

    struct State {
        std::mutex mu;
        std::deque<Buffer*> free;
        std::vector<std::unique_ptr<Buffer>> owned;
        std::size_t buffer_capacity{0};
    };

    explicit ByteBufferLease(std::shared_ptr<State> st, Buffer* b) : state_(std::move(st)), buf_(b) {}

    void reset();

    std::shared_ptr<State> state_;
    Buffer* buf_{nullptr};
    std::size_t size_{0};
};

// 固定容量的 buffer pool。
// - 预分配 N 个大小为 `buffer_capacity` 的 buffer。
// - try_acquire() 为非阻塞，设计用于 DDS 回调线程。
class ByteBufferPool {
public:
    struct Options {
        std::size_t buffers{64};
        std::size_t buffer_capacity{8192};
    };

    explicit ByteBufferPool(Options opts);

    // 非阻塞：当 pool 耗尽时返回 empty。
    std::optional<ByteBufferLease> try_acquire();

    std::size_t capacity_bytes() const;
    std::size_t free_buffers() const;

private:
    std::shared_ptr<ByteBufferLease::State> state_;
};

} // namespace wxz::core
