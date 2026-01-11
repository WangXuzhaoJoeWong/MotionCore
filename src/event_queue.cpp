#include "internal/event_queue.h"

#include "observability.h"

#include <chrono>

namespace wxz {

EventQueue::EventQueue(EventQueueOptions opts) : opts_(std::move(opts)) {
    if (opts_.high_watermark == 0 || opts_.high_watermark > opts_.max_size) {
        opts_.high_watermark = opts_.max_size;
    }
}

bool EventQueue::push(Event ev, bool *dropped) {
    bool dropped_flag = false;
    if (dropped) *dropped = false;
    std::unique_lock<std::mutex> lk(mtx_);
    if (stopped_) return false;

    auto can_push = [&]() { return queue_.size() < opts_.max_size || stopped_.load(); };

    if (opts_.block_when_full) {
        cv_not_full_.wait(lk, can_push);
        if (stopped_) return false;
    } else {
        if (queue_.size() >= opts_.max_size) {
            if (opts_.drop_oldest && !queue_.empty()) {
                queue_.pop_front();
                dropped_flag = true;
                if (dropped) *dropped = true;
            } else {
                publish(queue_.size(), false);
                return false;
            }
        }
    }

    queue_.push_back(std::move(ev));
    auto sz = queue_.size();
    lk.unlock();
    publish(sz, dropped_flag);
    cv_not_empty_.notify_one();
    return true;
}

bool EventQueue::pop(Event &out, int timeout_ms) {
    std::unique_lock<std::mutex> lk(mtx_);
    if (timeout_ms < 0) {
        cv_not_empty_.wait(lk, [&]() { return stopped_ || !queue_.empty(); });
    } else {
        cv_not_empty_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]() { return stopped_ || !queue_.empty(); });
    }

    if (queue_.empty()) {
        return false;
    }

    out = std::move(queue_.front());
    queue_.pop_front();
    auto sz = queue_.size();
    lk.unlock();
    publish(sz, false);
    cv_not_full_.notify_one();
    return true;
}

bool EventQueue::try_pop(Event &out) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    publish(queue_.size(), false);
    cv_not_full_.notify_one();
    return true;
}

size_t EventQueue::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return queue_.size();
}

void EventQueue::stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stopped_) return;
        stopped_.store(true);
    }
    cv_not_full_.notify_all();
    cv_not_empty_.notify_all();
}

void EventQueue::publish(size_t size, bool dropped) {
    auto hook = opts_.metrics_hook;
    if (hook) hook(EventQueueMetrics{size, dropped});

    if (wxz::core::has_metrics_sink()) {
        wxz::core::metrics().gauge_set("wxz.event_queue.size", static_cast<double>(size), {});
        if (dropped) {
            wxz::core::metrics().counter_add("wxz.event_queue.dropped", 1, {});
        }
    }
}

} // namespace wxz
