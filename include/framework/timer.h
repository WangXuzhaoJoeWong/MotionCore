#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "executor.h"
#include "strand.h"

#include "framework/time.h"

namespace wxz::framework {

/// ROS2-like wall timer：
/// - 计时基于 steady clock。
/// - 回调不直接执行：统一投递到 Executor/Strand。
/// - 由于 core::Executor 当前不支持延时任务，本实现需要在主循环中显式 tick()。
class TimerManager {
public:
    using Callback = std::function<void()>;

    struct TimerHandle {
        std::size_t id{0};
    };

    TimerManager() = default;

    void bind_scheduler(wxz::core::Executor& ex) {
        ex_ = &ex;
        strand_ = nullptr;
    }

    void bind_scheduler(wxz::core::Strand& strand) {
        strand_ = &strand;
        ex_ = nullptr;
    }

    /// 创建 wall timer。
    /// - period<=0：等同于禁用（仍会返回 handle）。
    TimerHandle create_wall_timer(std::chrono::milliseconds period, Callback cb) {
        std::lock_guard<std::mutex> lock(mu_);
        const std::size_t id = ++next_id_;
        Timer t;
        t.id = id;
        t.period = period;
        t.next_fire = steady_now() + period;
        t.cb = std::move(cb);
        timers_.push_back(std::move(t));
        return TimerHandle{id};
    }

    /// 在主循环里周期调用：触发到期 timer，并把回调投递到 scheduler。
    /// - 返回 true 表示至少触发并投递了一个回调。
    bool tick() {
        const auto now = steady_now();
        std::vector<Callback> to_fire;

        {
            std::lock_guard<std::mutex> lock(mu_);
            for (auto& t : timers_) {
                if (!t.enabled) continue;
                if (t.period.count() <= 0) continue;
                if (now < t.next_fire) continue;

                // 追赶：避免 tick 延迟导致漂移。
                while (t.next_fire <= now) {
                    t.next_fire += t.period;
                }

                if (t.cb) to_fire.push_back(t.cb);
            }
        }

        bool any = false;
        for (auto& cb : to_fire) {
            any = true;
            dispatch(std::move(cb));
        }
        return any;
    }

    /// 禁用一个 timer（不删除，保持 id 稳定）。
    void cancel(TimerHandle h) {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& t : timers_) {
            if (t.id == h.id) {
                t.enabled = false;
                return;
            }
        }
    }

private:
    struct Timer {
        std::size_t id{0};
        bool enabled{true};
        std::chrono::milliseconds period{0};
        std::chrono::steady_clock::time_point next_fire{};
        Callback cb;
    };

    void dispatch(Callback cb) {
        if (strand_) {
            (void)strand_->post([cb = std::move(cb)] { cb(); });
            return;
        }
        if (ex_) {
            (void)ex_->post([cb = std::move(cb)] { cb(); });
            return;
        }
        // 未绑定 scheduler：退化为直接执行（不推荐，但避免 silent drop）。
        cb();
    }

    std::mutex mu_;
    std::vector<Timer> timers_;
    std::size_t next_id_{0};

    wxz::core::Executor* ex_{nullptr};
    wxz::core::Strand* strand_{nullptr};
};

} // namespace wxz::framework
