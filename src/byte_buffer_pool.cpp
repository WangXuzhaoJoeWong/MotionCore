#include "byte_buffer_pool.h"

namespace wxz::core {

void ByteBufferLease::reset() {
    if (!buf_) return;
    if (state_) {
        std::lock_guard<std::mutex> lock(state_->mu);
        state_->free.push_back(buf_);
    }
    buf_ = nullptr;
    size_ = 0;
    state_.reset();
}

ByteBufferPool::ByteBufferPool(Options opts) : state_(std::make_shared<ByteBufferLease::State>()) {
    state_->buffer_capacity = opts.buffer_capacity;
    state_->owned.reserve(opts.buffers);

    for (std::size_t i = 0; i < opts.buffers; ++i) {
        auto b = std::make_unique<ByteBufferLease::Buffer>();
        b->data.resize(opts.buffer_capacity);
        state_->free.push_back(b.get());
        state_->owned.emplace_back(std::move(b));
    }
}

std::optional<ByteBufferLease> ByteBufferPool::try_acquire() {
    std::lock_guard<std::mutex> lock(state_->mu);
    if (state_->free.empty()) return std::nullopt;
    auto* b = state_->free.front();
    state_->free.pop_front();
    return ByteBufferLease(state_, b);
}

std::size_t ByteBufferPool::capacity_bytes() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->owned.size() * state_->buffer_capacity;
}

std::size_t ByteBufferPool::free_buffers() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->free.size();
}

} // namespace wxz::core
