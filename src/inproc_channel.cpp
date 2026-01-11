#include "inproc_channel.h"

#include "observability.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <thread>

namespace wxz::core {

// BufferHandle ---------------------------------------------------------------

void BufferHandle::commit(std::size_t size) {
    size_ = size;
    if (pool_) {
        pool_->set_committed_size(idx_, size_);
    }
}

void BufferHandle::release() {
    if (pool_) {
        pool_->release(idx_);
    }
    pool_ = nullptr;
    idx_ = static_cast<std::size_t>(-1);
    data_ = nullptr;
    capacity_ = 0;
    size_ = 0;
}

// BufferPool -----------------------------------------------------------------

BufferPool::BufferPool(std::size_t capacity, std::size_t buffer_bytes)
    : nodes_(capacity), free_head_(0), buffer_bytes_(buffer_bytes) {
    for (std::size_t i = 0; i < capacity; ++i) {
        nodes_[i].buf.resize(buffer_bytes_, 0);
        nodes_[i].next.store(i + 1, std::memory_order_relaxed);
        nodes_[i].len.store(0, std::memory_order_relaxed);
    }
    if (!nodes_.empty()) {
        nodes_.back().next.store(static_cast<std::size_t>(-1), std::memory_order_relaxed);
    } else {
        free_head_.store(static_cast<std::size_t>(-1), std::memory_order_relaxed);
    }
}

BufferHandle BufferPool::acquire() {
    std::size_t head = free_head_.load(std::memory_order_acquire);
    while (head != static_cast<std::size_t>(-1)) {
        Node& n = nodes_[head];
        std::size_t next = n.next.load(std::memory_order_relaxed);
        if (free_head_.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
            n.len.store(0, std::memory_order_relaxed);
            return BufferHandle(this, head, n.buf.data(), n.buf.size());
        }
    }
    return {};
}

void BufferPool::release(std::size_t idx) {
    if (idx >= nodes_.size()) return;
    std::size_t head = free_head_.load(std::memory_order_acquire);
    do {
        nodes_[idx].next.store(head, std::memory_order_relaxed);
    } while (!free_head_.compare_exchange_weak(head, idx, std::memory_order_acq_rel, std::memory_order_acquire));
}

std::uint8_t* BufferPool::data(std::size_t idx) {
    if (idx >= nodes_.size()) return nullptr;
    return nodes_[idx].buf.data();
}

std::size_t BufferPool::committed_size(std::size_t idx) const {
    if (idx >= nodes_.size()) return 0;
    return nodes_[idx].len.load(std::memory_order_acquire);
}

void BufferPool::set_committed_size(std::size_t idx, std::size_t size) {
    if (idx >= nodes_.size()) return;
    nodes_[idx].len.store(size, std::memory_order_release);
}

// IndexQueue -----------------------------------------------------------------

IndexQueue::IndexQueue(std::size_t capacity) : buffer_(capacity), mask_(capacity - 1), capacity_(capacity) {
    // require power-of-two capacity for mask
    if ((capacity & (capacity - 1)) != 0) {
        throw std::invalid_argument("IndexQueue capacity must be power of two");
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        buffer_[i].seq.store(i, std::memory_order_relaxed);
    }
}

bool IndexQueue::enqueue(std::size_t v) {
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    for (;;) {
        Cell& cell = buffer_[pos & mask_];
        std::size_t seq = cell.seq.load(std::memory_order_acquire);
        intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        if (dif == 0) {
            if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                cell.data = v;
                cell.seq.store(pos + 1, std::memory_order_release);
                return true;
            }
        } else if (dif < 0) {
            return false; // full
        } else {
            pos = enqueue_pos_.load(std::memory_order_relaxed);
        }
    }
}

bool IndexQueue::dequeue(std::size_t& v) {
    std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    for (;;) {
        Cell& cell = buffer_[pos & mask_];
        std::size_t seq = cell.seq.load(std::memory_order_acquire);
        intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        if (dif == 0) {
            if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                v = cell.data;
                cell.seq.store(pos + buffer_.size(), std::memory_order_release);
                return true;
            }
        } else if (dif < 0) {
            return false; // empty
        } else {
            pos = dequeue_pos_.load(std::memory_order_relaxed);
        }
    }
}

std::size_t IndexQueue::dequeue_batch(std::size_t* out, std::size_t max_items) {
    std::size_t count = 0;
    while (count < max_items) {
        std::size_t value = 0;
        if (!dequeue(value)) break;
        out[count++] = value;
    }
    return count;
}

// InprocChannel --------------------------------------------------------------

InprocChannel::InprocChannel(std::size_t capacity, std::size_t buffer_bytes, const ChannelQoS& qos)
    : qos_(qos), pool_(capacity, buffer_bytes), queue_(capacity) {}

InprocChannel::~InprocChannel() { stop(); }

BufferHandle InprocChannel::allocate() { return pool_.acquire(); }

bool InprocChannel::publish(BufferHandle&& h) {
    if (!h.valid()) return false;
    std::size_t idx = h.idx_;
    // the size may already be committed via handle.commit(); ensure not zero unless intended
    if (pool_.committed_size(idx) == 0) {
        pool_.set_committed_size(idx, h.size());
    }
    // ownership transfers to queue; prevent double release
    h.pool_ = nullptr;
    h.data_ = nullptr;
    h.capacity_ = 0;
    h.size_ = 0;
    bool ok = queue_.enqueue(idx);
    if (ok) {
        cv_.notify_one();
        ++publish_success_;
        if (wxz::core::has_metrics_sink()) {
            wxz::core::metrics().counter_add("wxz.inproc.publish.success", 1, {});
        }
    } else {
        pool_.release(idx);
        ++publish_fail_;
        if (wxz::core::has_metrics_sink()) {
            wxz::core::metrics().counter_add("wxz.inproc.publish.fail", 1, {});
        }
    }
    return ok;
}

bool InprocChannel::publish(const std::uint8_t* data, std::size_t size) {
    BufferHandle h = allocate();
    if (!h.valid() || size > h.capacity()) return false;
    std::copy_n(data, size, h.data());
    h.commit(size);
    return publish(std::move(h));
}

void InprocChannel::subscribe(Handler handler) {
    auto sub = subscribe_scoped(std::move(handler), nullptr);
    sub.detach();
}

Subscription InprocChannel::subscribe_scoped(Handler handler, void* owner) {
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        id = next_handler_id_++;
        handlers_.push_back(HandlerEntry{.id = id, .owner = owner, .handler = std::move(handler)});
    }

    bool expected = false;
    if (running_.compare_exchange_strong(expected, true)) {
        worker_ = std::thread([this]() { dispatch_loop(); });
    }

    return Subscription([this, id]() {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(), [&](const HandlerEntry& e) {
                           return e.id == id;
                       }),
                       handlers_.end());
    });
}

void InprocChannel::unsubscribe_owner(void* owner) {
    if (!owner) return;
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(), [&](const HandlerEntry& e) {
                       return e.owner == owner;
                   }),
                   handlers_.end());
}

void InprocChannel::stop() {
    const bool was_running = running_.exchange(false);
    if (was_running) {
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handlers_.clear();
    }
}

void InprocChannel::dispatch_loop() {
    constexpr std::size_t kBatch = 32;
    std::size_t batch[kBatch];

    while (running_.load(std::memory_order_relaxed)) {
        std::size_t n = queue_.dequeue_batch(batch, kBatch);
        if (n == 0) {
            std::unique_lock<std::mutex> lk(wait_mutex_);
            cv_.wait_for(lk, std::chrono::microseconds(50));
            continue;
        }

        std::vector<Handler> copy_handlers;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            copy_handlers.reserve(handlers_.size());
            for (const auto& e : handlers_) copy_handlers.push_back(e.handler);
        }

        for (std::size_t i = 0; i < n; ++i) {
            auto idx = batch[i];
            auto* buf = pool_.data(idx);
            std::size_t sz = pool_.committed_size(idx);
            for (auto& h : copy_handlers) {
                if (h) h(buf, sz);
            }
            messages_delivered_.fetch_add(1, std::memory_order_relaxed);
            pool_.release(idx);
        }
    }
    // drain remaining items to release buffers
    std::size_t idx = 0;
    while (queue_.dequeue(idx)) {
        pool_.release(idx);
    }
}

} // namespace wxz::core
