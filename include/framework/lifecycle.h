#pragma once

#include <functional>
#include <string>
#include <utility>

#include "framework/status.h"

namespace wxz::framework {

/// 最小 Lifecycle 状态机（对标 rclcpp_lifecycle 的核心概念）：
/// - unconfigured -> inactive -> active
/// - deactivate 回到 inactive
/// - shutdown 进入 finalized
///
/// 说明：
/// - 这是“框架层约定”，不强绑定具体通信/线程模型；业务可把 on_* 钩子里的工作投递到 strand。
class Lifecycle {
public:
    enum class State {
        unconfigured,
        inactive,
        active,
        finalized,
    };

    using Hook = std::function<Status()>;

    struct Hooks {
        Hook on_configure;
        Hook on_activate;
        Hook on_deactivate;
        Hook on_shutdown;
    };

    Lifecycle() = default;
    explicit Lifecycle(Hooks hooks) : hooks_(std::move(hooks)) {}

    State state() const { return state_; }

    bool is_active() const { return state_ == State::active; }

    Status configure() {
        if (state_ != State::unconfigured) return bad_transition("configure");
        Status st = call(hooks_.on_configure);
        if (st.ok) state_ = State::inactive;
        return st;
    }

    Status activate() {
        if (state_ != State::inactive) return bad_transition("activate");
        Status st = call(hooks_.on_activate);
        if (st.ok) state_ = State::active;
        return st;
    }

    Status deactivate() {
        if (state_ != State::active) return bad_transition("deactivate");
        Status st = call(hooks_.on_deactivate);
        if (st.ok) state_ = State::inactive;
        return st;
    }

    Status shutdown() {
        if (state_ == State::finalized) return Status::ok_status();
        Status st = call(hooks_.on_shutdown);
        // shutdown 一旦调用就进入 finalized；失败由 status 表达。
        state_ = State::finalized;
        return st;
    }

private:
    static Status call(const Hook& h) {
        if (!h) return Status::ok_status();
        return h();
    }

    static Status bad_transition(const char* op) {
        Status st;
        st.ok = false;
        st.err_code = 1;
        st.err = std::string("bad_transition:") + op;
        return st;
    }

    State state_{State::unconfigured};
    Hooks hooks_;
};

inline const char* to_string(Lifecycle::State s) {
    switch (s) {
        case Lifecycle::State::unconfigured:
            return "unconfigured";
        case Lifecycle::State::inactive:
            return "inactive";
        case Lifecycle::State::active:
            return "active";
        case Lifecycle::State::finalized:
            return "finalized";
    }
    return "unknown";
}

} // namespace wxz::framework
