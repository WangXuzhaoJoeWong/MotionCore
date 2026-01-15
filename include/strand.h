#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

#include "executor.h"
#include "observability.h"

namespace wxz::core {

// Strand: serializes tasks onto an underlying Executor.
// Guarantees FIFO execution order for tasks posted through the same Strand.
class Strand {
public:
    explicit Strand(Executor& ex) : ex_(&ex) {}
    Strand(const Strand&) = delete;
    Strand& operator=(const Strand&) = delete;

    template <class F>
    bool post(F&& fn) {
        MoveOnlyFunction task(std::forward<F>(fn));
        if (!task) return true;
        if (stopped_.load()) {
            if (wxz::core::has_metrics_sink()) {
                wxz::core::metrics().counter_add("wxz.strand.post.reject", 1, {{"reason", "stopped"}});
            }
            return false;
        }

        bool need_schedule = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stopped_.load()) {
                if (wxz::core::has_metrics_sink()) {
                    wxz::core::metrics().counter_add("wxz.strand.post.reject", 1, {{"reason", "stopped"}});
                }
                return false;
            }
            q_.push_back(std::move(task));
            if (!scheduled_) {
                scheduled_ = true;
                need_schedule = true;
            }
        }

        if (need_schedule) {
            const bool ok = ex_->post([this] { drain(); });
            if (!ok) {
                if (wxz::core::has_metrics_sink()) {
                    wxz::core::metrics().counter_add("wxz.strand.post.reject", 1, {{"reason", "executor_rejected"}});
                }
            }
            return ok;
        }
        return true;
    }

    void stop() {
        stopped_.store(true);
        std::lock_guard<std::mutex> lock(mu_);
        q_.clear();
    }

private:
    void drain() {
        for (;;) {
            MoveOnlyFunction task;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (q_.empty()) {
                    scheduled_ = false;
                    return;
                }
                task = std::move(q_.front());
                q_.pop_front();
            }

            task();
        }
    }

    Executor* ex_{nullptr};
    std::mutex mu_;
    std::deque<MoveOnlyFunction> q_;
    bool scheduled_{false};
    std::atomic<bool> stopped_{false};
};

} // namespace wxz::core
