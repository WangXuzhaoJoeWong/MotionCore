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
    std::string type;      // e.g. message, timer, peer_update
    std::string source;    // producer identifier
    std::string context;   // optional context key
    uint8_t attempt{0};    // delivery attempts for retry logic
    std::vector<uint8_t> payload;
    std::chrono::steady_clock::time_point enqueue_ts{std::chrono::steady_clock::now()};
};

struct EventQueueMetrics {
    size_t size{0};
    bool dropped{false};
};

struct EventQueueOptions {
    size_t max_size{1024};            // hard cap
    size_t high_watermark{900};       // soft cap to trigger drop_oldest
    bool block_when_full{true};
    bool drop_oldest{true};           // if not blocking and over watermark, drop oldest
    std::function<void(const EventQueueMetrics &)> metrics_hook;
};

class EventQueue {
public:
    explicit EventQueue(EventQueueOptions opts = {});

    // Blocking push when block_when_full=true. Returns false if stopped.
    bool push(Event ev, bool *dropped = nullptr);

    // Blocking pop; returns false if stopped and queue empty. If timeout_ms>=0, waits up to timeout.
    bool pop(Event &out, int timeout_ms = -1);

    // Non-blocking; returns true if an event was popped.
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
