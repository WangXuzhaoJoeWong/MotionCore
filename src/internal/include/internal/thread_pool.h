#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "internal/threading_config.h"

namespace wxz {

struct ThreadPoolMetrics {
    size_t queue_size{0};
    size_t tasks_running{0};
};

struct ThreadPoolOptions {
    std::string name;
    size_t max_queue{1024};
    bool block_when_full{true};
    size_t threads{0}; // 0 -> derive from config/defaults
    std::function<void(const ThreadPoolMetrics &)> metrics_hook;
};

// 有界线程池：支持可选 metrics hook，并提供背压控制。
class ThreadPool {
public:
    ThreadPool(std::string module_key, ThreadPoolOptions opts, int default_threads, int max_threads);
    ~ThreadPool();

    // 启动 worker 线程；如果已在运行则返回 false。
    bool start();

    // 停止 worker 并清空队列。可重复调用（幂等）。
    void stop();

    // 提交任务：若已停止或队列拒绝该任务则返回 false。
    bool submit(std::function<void()> fn);

    bool running() const { return running_.load(); }
    size_t queueSize() const;
    std::string name() const { return name_; }

private:
    void workerLoop(int worker_id);
    void publishMetrics(size_t queue_size, size_t tasks_running) const;

    std::string module_key_;
    std::string name_;
    ThreadPoolOptions opts_;
    int default_threads_;
    int max_threads_;

    mutable std::mutex mtx_;
    std::condition_variable cv_task_;
    std::condition_variable cv_not_full_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    size_t tasks_running_{0};
};

// IO 取向线程池：默认规模较小；遵循配置项 `threading.io_pool`。
class IoThreadPool : public ThreadPool {
public:
    explicit IoThreadPool(ThreadPoolOptions opts = {});
};

// CPU 取向线程池：默认使用 hardware_concurrency；遵循配置项 `threading.cpu_pool`。
class CpuThreadPool : public ThreadPool {
public:
    explicit CpuThreadPool(ThreadPoolOptions opts = {});
};

} // namespace wxz
