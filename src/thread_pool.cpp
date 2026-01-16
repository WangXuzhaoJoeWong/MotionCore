#include "internal/thread_pool.h"

#include <algorithm>
#include <utility>

namespace wxz {
namespace {
int default_cpu_threads() {
    unsigned c = std::thread::hardware_concurrency();
    return c == 0 ? 4 : static_cast<int>(c);
}
}

ThreadPool::ThreadPool(std::string module_key, ThreadPoolOptions opts, int default_threads, int max_threads)
    : module_key_(std::move(module_key)),
      name_(opts.name.empty() ? module_key_ : opts.name),
      opts_(std::move(opts)),
      default_threads_(default_threads),
      max_threads_(max_threads) {
    if (opts_.name.empty()) {
        opts_.name = name_;
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

bool ThreadPool::start() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (running_) return false;

    stopping_.store(false);
    size_t threads = opts_.threads > 0 ? opts_.threads
                                       : static_cast<size_t>(get_thread_count_for_module(module_key_, default_threads_, max_threads_));
    if (threads == 0) threads = static_cast<size_t>(default_threads_);
    if (threads == 0) threads = 1;
    threads = static_cast<size_t>(std::clamp<int>(static_cast<int>(threads), 1, max_threads_));

    workers_.clear();
    workers_.reserve(threads);
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this, i]() { workerLoop(static_cast<int>(i)); });
    }

    running_.store(true);
    return true;
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_) return;
        stopping_.store(true);
    }

    cv_task_.notify_all();
    cv_not_full_.notify_all();

    for (auto &t : workers_) {
        if (t.joinable()) t.join();
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        workers_.clear();
        tasks_.clear();
        tasks_running_ = 0;
        running_.store(false);
        stopping_.store(false);
    }
}

bool ThreadPool::submit(std::function<void()> fn) {
    if (!fn) return false;

    size_t queue_snapshot = 0;
    {
        std::unique_lock<std::mutex> lk(mtx_);
        if (!running_ || stopping_) return false;

        if (opts_.max_queue > 0) {
            if (!opts_.block_when_full) {
                if (tasks_.size() >= opts_.max_queue) return false;
            } else {
                cv_not_full_.wait(lk, [&]() {
                    return stopping_.load() || tasks_.size() < opts_.max_queue;
                });
                if (stopping_) return false;
                if (!running_) return false;
            }
        }

        tasks_.push_back(std::move(fn));
        queue_snapshot = tasks_.size();
    }

    publishMetrics(queue_snapshot, tasks_running_);
    cv_task_.notify_one();
    return true;
}

size_t ThreadPool::queueSize() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return tasks_.size();
}

void ThreadPool::workerLoop(int /*worker_id*/) {
    for (;;) {
        std::function<void()> task;
        size_t queue_snapshot = 0;

        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_task_.wait(lk, [&]() { return stopping_.load() || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) break;

            task = std::move(tasks_.front());
            tasks_.pop_front();
            ++tasks_running_;
            queue_snapshot = tasks_.size();
            cv_not_full_.notify_one();
        }

        publishMetrics(queue_snapshot, tasks_running_);

        try {
            task();
        } catch (...) {
            // 吞掉异常以保持线程池存活。
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (tasks_running_ > 0) --tasks_running_;
            queue_snapshot = tasks_.size();
        }

        publishMetrics(queue_snapshot, tasks_running_);
    }
}

void ThreadPool::publishMetrics(size_t queue_size, size_t tasks_running) const {
    auto hook = opts_.metrics_hook;
    if (hook) {
        hook(ThreadPoolMetrics{queue_size, tasks_running});
    }
}

IoThreadPool::IoThreadPool(ThreadPoolOptions opts)
    : ThreadPool("io_pool", std::move(opts), 2, 32) {}

CpuThreadPool::CpuThreadPool(ThreadPoolOptions opts)
    : ThreadPool("cpu_pool", std::move(opts), default_cpu_threads(), 64) {}

} // namespace wxz
