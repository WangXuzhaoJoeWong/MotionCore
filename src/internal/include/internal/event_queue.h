#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace wxz {

struct Event {
    uint64_t id{0};
    std::string type;      // 例如 message / timer / peer_update
    std::string source;    // 生产者标识
    std::string context;   // 可选上下文 key
    uint8_t attempt{0};    // 重试逻辑的投递次数
    std::vector<uint8_t> payload;
    std::chrono::steady_clock::time_point enqueue_ts{std::chrono::steady_clock::now()};
};

struct EventQueueMetrics {
    size_t size{0};
    bool dropped{false};
};

struct EventQueueOptions {
    size_t max_size{1024};            // 硬上限
    size_t high_watermark{900};       // 软上限：触发 drop_oldest
    bool block_when_full{true};
    bool drop_oldest{true};           // 不阻塞且超过 watermark 时，丢弃最旧消息
    std::function<void(const EventQueueMetrics &)> metrics_hook;
};

class EventQueue {
public:
    explicit EventQueue(EventQueueOptions opts = {});

    // block_when_full=true 时为阻塞 push；若已 stop 则返回 false。
    bool push(Event ev, bool *dropped = nullptr);

    // 阻塞 pop：若已 stop 且队列为空则返回 false。timeout_ms>=0 时最多等待该时长。
    bool pop(Event &out, int timeout_ms = -1);

    // 非阻塞：若成功弹出事件则返回 true。
    bool try_pop(Event &out);

    size_t size() const;

    void stop();
    bool stopped() const { return stopped_.load(); }

private:
    void publish(size_t size, bool dropped);

    EventQueueOptions opts_;
    mutable std::mutex mtx_;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_not_empty_;
    std::deque<Event> queue_;
    std::atomic<bool> stopped_{false};
};

} // namespace wxz
