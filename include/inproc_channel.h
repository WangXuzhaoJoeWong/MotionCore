#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "subscription.h"

namespace wxz::core {

struct ChannelQoS {
    enum class Reliability { best_effort, reliable };
    enum class Durability { volatile_kind, transient_local };
    enum class Liveliness { automatic, manual_by_topic };
    enum class Ownership { shared, exclusive };
    Reliability reliability{Reliability::reliable};
    std::size_t history{1};          // keep_last N；0 表示 keep_all（上限受 capacity 约束）
    std::uint64_t deadline_ns{0};    // 0 表示未设置（unset）
    std::uint64_t latency_budget_ns{0};
    Durability durability{Durability::volatile_kind};
    Liveliness liveliness{Liveliness::automatic};
    std::uint64_t lifespan_ns{0};            // 丢弃早于 lifespan 的样本
    std::uint64_t time_based_filter_ns{0};   // reader 侧最小时间间隔（minimum separation）
    Ownership ownership{Ownership::shared};
    std::int32_t ownership_strength{0};      // 仅在 ownership == exclusive 时生效
    std::int32_t transport_priority{0};
    bool async_publish{false};               // fastdds：异步发布模式
    bool realtime_hint{false};

    static ChannelQoS realtime_preset(std::size_t depth = 8) {
        ChannelQoS q;
        q.reliability = Reliability::reliable;
        q.history = depth;
        q.durability = Durability::volatile_kind;
        q.liveliness = Liveliness::automatic;
        q.async_publish = false;
        q.realtime_hint = true;
        q.transport_priority = 99;
        q.latency_budget_ns = 1'000'000; // 1 ms 提示
        q.deadline_ns = 2'000'000;       // 默认 2 ms deadline 目标
        return q;
    }
};

class BufferPool;

// RAII buffer 句柄：由预分配池提供；用于进程内传输的零拷贝发布路径。
class BufferHandle {
public:
    BufferHandle() = default;
    BufferHandle(BufferHandle&& other) noexcept { move_from(std::move(other)); }
    BufferHandle& operator=(BufferHandle&& other) noexcept {
        if (this != &other) {
            release();
            move_from(std::move(other));
        }
        return *this;
    }
    BufferHandle(const BufferHandle&) = delete;
    BufferHandle& operator=(const BufferHandle&) = delete;
    ~BufferHandle() { release(); }

    std::uint8_t* data() { return data_; }
    const std::uint8_t* data() const { return data_; }
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return size_; }
    void commit(std::size_t size);
    bool valid() const { return pool_ != nullptr; }
private:
    friend class BufferPool;
    friend class InprocChannel;
    BufferPool* pool_{nullptr};
    std::size_t idx_{static_cast<std::size_t>(-1)};
    std::uint8_t* data_{nullptr};
    std::size_t capacity_{0};
    std::size_t size_{0};

    BufferHandle(BufferPool* pool, std::size_t idx, std::uint8_t* data, std::size_t cap)
        : pool_(pool), idx_(idx), data_(data), capacity_(cap), size_(0) {}

    void release();
    void move_from(BufferHandle&& other) {
        pool_ = other.pool_;
        idx_ = other.idx_;
        data_ = other.data_;
        capacity_ = other.capacity_;
        size_ = other.size_;
        other.pool_ = nullptr;
        other.idx_ = static_cast<std::size_t>(-1);
        other.data_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
    }
};

class BufferPool {
public:
    BufferPool(std::size_t capacity, std::size_t buffer_bytes);
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    BufferHandle acquire();
    void release(std::size_t idx);
    std::size_t buffer_size() const { return buffer_bytes_; }
    std::uint8_t* data(std::size_t idx);
    std::size_t committed_size(std::size_t idx) const;
    void set_committed_size(std::size_t idx, std::size_t size);

private:
    struct Node {
        std::vector<std::uint8_t> buf;
        std::atomic<std::size_t> next;
        std::atomic<std::size_t> len;
    };

    std::vector<Node> nodes_;
    std::atomic<std::size_t> free_head_;
    std::size_t buffer_bytes_{0};
};

// 有界 MPMC 无锁队列（Dmitry Vyukov 变体），用于存放 buffer pool 的索引。
class IndexQueue {
public:
    explicit IndexQueue(std::size_t capacity);
    IndexQueue(const IndexQueue&) = delete;
    IndexQueue& operator=(const IndexQueue&) = delete;

    bool enqueue(std::size_t v);
    bool dequeue(std::size_t& v);
    std::size_t capacity() const { return capacity_; }

    // 尝试出队最多 max_items 个元素到调用方提供的缓冲区；返回实际数量。
    std::size_t dequeue_batch(std::size_t* out, std::size_t max_items);

private:
    struct Cell {
        std::atomic<std::size_t> seq;
        std::size_t data;
    };

    std::vector<Cell> buffer_;
    std::size_t mask_{0};
    std::atomic<std::size_t> enqueue_pos_{0};
    std::atomic<std::size_t> dequeue_pos_{0};
    std::size_t capacity_{0};
};

class InprocChannel {
public:
    using Handler = std::function<void(const std::uint8_t* data, std::size_t size)>;

    InprocChannel(std::size_t capacity, std::size_t buffer_bytes, const ChannelQoS& qos = {});
    ~InprocChannel();

    // 零拷贝发布路径：分配 buffer -> 填充 -> commit -> publish。
    BufferHandle allocate();
    bool publish(BufferHandle&& h);

    // 便捷的拷贝发布路径。
    bool publish(const std::uint8_t* data, std::size_t size);

    // 注册 handler；首次订阅时启动分发线程。
    void subscribe(Handler handler);

    // 带作用域的订阅（可显式取消）。
    // owner 为可选 tag（例如插件实例指针），用于批量清理。
    Subscription subscribe_scoped(Handler handler, void* owner = nullptr);

    // 批量取消：移除所有带指定 owner tag 的 handler。
    void unsubscribe_owner(void* owner);

    // 停止分发（会尽力 drain 当前队列）。
    void stop();

    // 可观测性
    std::uint64_t publish_success() const { return publish_success_.load(); }
    std::uint64_t publish_fail() const { return publish_fail_.load(); }
    std::uint64_t messages_delivered() const { return messages_delivered_.load(); }

private:
    void dispatch_loop();

    struct HandlerEntry {
        std::uint64_t id{0};
        void* owner{nullptr};
        Handler handler;
    };

    ChannelQoS qos_;
    BufferPool pool_;
    IndexQueue queue_;
    std::vector<HandlerEntry> handlers_;
    std::uint64_t next_handler_id_{1};
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::condition_variable cv_;
    std::mutex wait_mutex_;
    std::mutex handler_mutex_;

    std::atomic<std::uint64_t> publish_success_{0};
    std::atomic<std::uint64_t> publish_fail_{0};
    std::atomic<std::uint64_t> messages_delivered_{0};
};

} // namespace wxz::core
