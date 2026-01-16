#include "internal/event_dispatcher.h"

#include <utility>

namespace wxz {

EventDispatcher::EventDispatcher(EventQueue &queue, IoThreadPool &io_pool, CpuThreadPool &cpu_pool, DispatchOptions opts)
    : queue_(queue), io_pool_(io_pool), cpu_pool_(cpu_pool), opts_(std::move(opts)) {}

EventDispatcher::~EventDispatcher() {
    stop();
}

bool EventDispatcher::start() {
    if (running_.load()) return false;
    running_.store(true);
    loop_thread_ = std::thread([this]() { loop(); });
    return true;
}

void EventDispatcher::stop() {
    if (!running_.load()) return;
    running_.store(false);
    queue_.stop();
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

void EventDispatcher::loop() {
    Event ev;
    while (running_.load()) {
        bool ok = queue_.pop(ev, opts_.pop_timeout_ms);
        if (!ok) {
            continue; // timeout or stopped
        }
        dispatch(ev);
    }
}

void EventDispatcher::dispatch(Event ev) {
    const std::string lane = chooseLane(ev);
    auto submit_fn = [this, lane, ev = std::move(ev)]() mutable {
        bool ok = true;
        if (opts_.handler) {
            try {
                ok = opts_.handler(ev);
            } catch (...) {
                ok = false;
                if (opts_.error_hook) {
                    opts_.error_hook(ev, "handler threw exception");
                }
            }
        }
        handleResult(std::move(ev), ok, lane);
    };

    bool submitted = false;
    if (lane == "io") {
        submitted = io_pool_.submit(submit_fn);
    } else {
        submitted = cpu_pool_.submit(submit_fn);
    }

    if (!submitted) {
        if (opts_.error_hook) {
            opts_.error_hook(ev, "submit failed (pool stopped or queue full)");
        }
        handleResult(std::move(ev), false, lane);
    }
}

std::string EventDispatcher::chooseLane(const Event &ev) const {
    if (opts_.router) {
        std::string lane = opts_.router(ev);
        if (lane == "io" || lane == "cpu") return lane;
    }
    // 默认：基于前缀的启发式规则
    if (ev.type.rfind("io.", 0) == 0) return "io";
    return "cpu";
}

void EventDispatcher::handleResult(Event ev, bool ok, const std::string &lane) {
    if (ok) return;

    if (ev.attempt < static_cast<uint8_t>(opts_.max_retries)) {
        ++ev.attempt;
        bool dropped = false;
        bool requeued = queue_.push(std::move(ev), &dropped);
        if (opts_.retry_hook) {
            opts_.retry_hook(ev);
        }
        if (!requeued && opts_.dead_letter_hook) {
            opts_.dead_letter_hook(ev, dropped ? "requeue dropped (queue full)" : "requeue failed (stopped)");
        }
        return;
    }

    if (opts_.dead_letter_hook) {
        opts_.dead_letter_hook(ev, "max retries exceeded on lane " + lane);
    }
}

} // namespace wxz
