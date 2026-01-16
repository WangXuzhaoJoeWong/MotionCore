#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "internal/event_queue.h"
#include "internal/thread_pool.h"

namespace wxz {

struct DispatchOptions {
    size_t max_retries{2};
    int pop_timeout_ms{100};
    // 决定使用哪个 lane："io" 或 "cpu"。默认：type 以 "io." 为前缀走 IO，否则走 CPU。
    std::function<std::string(const Event &)> router;
    // Handler 成功返回 true；返回 false 会触发 retry/dead-letter。
    std::function<bool(const Event &)> handler;
    std::function<void(const Event &, const std::string &)> dead_letter_hook;
    std::function<void(const Event &, const std::string &)> error_hook;
    std::function<void(const Event &)> retry_hook;
};

class EventDispatcher {
public:
    EventDispatcher(EventQueue &queue, IoThreadPool &io_pool, CpuThreadPool &cpu_pool, DispatchOptions opts = {});
    ~EventDispatcher();

    bool start();
    void stop();
    bool running() const { return running_.load(); }

private:
    void loop();
    void dispatch(Event ev);
    std::string chooseLane(const Event &ev) const;
    void handleResult(Event ev, bool ok, const std::string &lane);

    EventQueue &queue_;
    IoThreadPool &io_pool_;
    CpuThreadPool &cpu_pool_;
    DispatchOptions opts_;
    std::thread loop_thread_;
    std::atomic<bool> running_{false};
};

} // namespace wxz
