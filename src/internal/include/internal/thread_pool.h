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

// Bounded thread pool with optional metrics hook and backpressure control.
class ThreadPool {
public:
    ThreadPool(std::string module_key, ThreadPoolOptions opts, int default_threads, int max_threads);
    ~ThreadPool();

    // Start worker threads; returns false if already running.
    bool start();

    // Stop workers and drain queue. Safe to call multiple times.
    void stop();

    // Submit a task. Returns false if stopped or queue rejected the task.
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

// IO-oriented pool defaults to modest size; honor config key `threading.io_pool`.
class IoThreadPool : public ThreadPool {
public:
    explicit IoThreadPool(ThreadPoolOptions opts = {});
};

// CPU-oriented pool defaults to hardware_concurrency; honor config key `threading.cpu_pool`.
class CpuThreadPool : public ThreadPool {
public:
    explicit CpuThreadPool(ThreadPoolOptions opts = {});
};

} // namespace wxz
