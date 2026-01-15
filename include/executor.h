#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "move_only_function.h"
#include "observability.h"

namespace wxz::core {

// Minimal fixed-size executor.
// - post(): enqueue a task for execution by worker threads.
// - stop(): stop accepting new tasks and drain queued tasks.
class Executor {
public:
    struct Options {
        // threads:
        // - >0: spawn N worker threads on start().
        // - =0: do not spawn threads; user drives execution via spin()/spin_once().
        std::size_t threads{1};
        std::size_t max_queue{1024};
        bool block_when_full{true};
    };

    Executor() = default;
    explicit Executor(Options opts) : opts_(std::move(opts)) {}
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    ~Executor() { stop(); }

    bool start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) return false;
        stopping_.store(false);
        if (opts_.threads > 0) {
            workers_.reserve(opts_.threads);
            for (std::size_t i = 0; i < opts_.threads; ++i) {
                workers_.emplace_back([this] { worker_loop(); });
            }
        }
        return true;
    }

    // Drive queued tasks on the calling thread until stop() is requested.
    // Intended for ROS2-like "single spin thread" usage when opts_.threads==0.
    void spin() {
        if (!running_.load() || stopping_.load()) return;
        worker_loop();
    }

    // Spin at most one task.
    // - Returns true if a task was executed.
    // - Returns false on timeout / no task / stopping.
    template <class Rep, class Period>
    bool spin_once(const std::chrono::duration<Rep, Period>& timeout) {
        if (!running_.load() || stopping_.load()) return false;

        MoveOnlyFunction task;
        {
            std::unique_lock<std::mutex> lock(mu_);
            if (tasks_.empty()) {
                cv_task_.wait_for(lock, timeout, [&] { return stopping_.load() || !tasks_.empty(); });
            }

            if (tasks_.empty() || stopping_.load()) return false;
            task = std::move(tasks_.front());
            tasks_.pop_front();
            cv_not_full_.notify_one();
        }

        task();
        return true;
    }

    void stop() {
        if (!running_.load()) return;
        stopping_.store(true);
        {
            std::lock_guard<std::mutex> lock(mu_);
            cv_task_.notify_all();
            cv_not_full_.notify_all();
        }
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
        running_.store(false);
    }

    template <class F>
    bool post(F&& fn) {
        MoveOnlyFunction task(std::forward<F>(fn));
        if (!task) return true;
        if (!running_.load()) {
            if (wxz::core::has_metrics_sink()) {
                wxz::core::metrics().counter_add("wxz.executor.post.reject", 1, {{"reason", "not_running"}});
            }
            return false;
        }
        if (stopping_.load()) {
            if (wxz::core::has_metrics_sink()) {
                wxz::core::metrics().counter_add("wxz.executor.post.reject", 1, {{"reason", "stopping"}});
            }
            return false;
        }

        std::unique_lock<std::mutex> lock(mu_);
        if (opts_.max_queue > 0) {
            if (opts_.block_when_full) {
                cv_not_full_.wait(lock, [&] {
                    return stopping_.load() || tasks_.size() < opts_.max_queue;
                });
            }
            if (stopping_.load()) {
                if (wxz::core::has_metrics_sink()) {
                    wxz::core::metrics().counter_add("wxz.executor.post.reject", 1, {{"reason", "stopping"}});
                }
                return false;
            }
            if (tasks_.size() >= opts_.max_queue) {
                if (wxz::core::has_metrics_sink()) {
                    wxz::core::metrics().counter_add("wxz.executor.post.reject", 1, {{"reason", "queue_full"}});
                }
                return false;
            }
        }

        tasks_.push_back(std::move(task));
        cv_task_.notify_one();
        return true;
    }

    bool running() const { return running_.load(); }

private:
    void worker_loop() {
        for (;;) {
            MoveOnlyFunction task;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_task_.wait(lock, [&] { return stopping_.load() || !tasks_.empty(); });

                if (tasks_.empty()) {
                    if (stopping_.load()) return;
                    continue;
                }

                task = std::move(tasks_.front());
                tasks_.pop_front();
                cv_not_full_.notify_one();
            }

            task();
        }
    }

    Options opts_;

    std::mutex mu_;
    std::condition_variable cv_task_;
    std::condition_variable cv_not_full_;
    std::deque<MoveOnlyFunction> tasks_;

    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
};

} // namespace wxz::core
