#pragma once

#include <chrono>
#include <cstddef>

#include "inproc_channel.h" // wxz::core::ChannelQoS
#include "service_common.h" // default_reliable_qos

namespace wxz::framework {

/// ROS2-like QoS 薄封装：
/// - 目标：让业务侧用更熟悉的 API 表达 QoS，而内部仍然走 MotionCore 的 ChannelQoS。
/// - 说明：当前只覆盖常用字段；如需更细粒度控制，可直接传 wxz::core::ChannelQoS。
class QoS {
public:
    using ChannelQoS = wxz::core::ChannelQoS;

    QoS() : qos_(wxz::core::default_reliable_qos()) {}
    explicit QoS(ChannelQoS qos) : qos_(qos) {}

    /// 可靠传输（默认推荐）。
    static QoS reliable() { return QoS(wxz::core::default_reliable_qos()); }

    /// 尽力而为传输（更低延迟/更少阻塞，但可能丢包）。
    static QoS best_effort() {
        ChannelQoS q = wxz::core::default_reliable_qos();
        q.reliability = ChannelQoS::Reliability::best_effort;
        return QoS(q);
    }

    /// keep_last(depth)：类似 ROS2 的 history depth。
    QoS& keep_last(std::size_t depth) {
        qos_.history = depth;
        return *this;
    }

    /// deadline：期望的消息间隔上限（用于监测/调度提示）。
    QoS& deadline(std::chrono::nanoseconds ns) {
        qos_.deadline_ns = static_cast<std::uint64_t>(ns.count());
        return *this;
    }

    /// latency_budget：调度层可以用于优化批处理/延迟。
    QoS& latency_budget(std::chrono::nanoseconds ns) {
        qos_.latency_budget_ns = static_cast<std::uint64_t>(ns.count());
        return *this;
    }

    /// 异步 publish：避免在 publish 调用处阻塞。
    QoS& async_publish(bool v = true) {
        qos_.async_publish = v;
        return *this;
    }

    const ChannelQoS& channel_qos() const { return qos_; }
    operator ChannelQoS() const { return qos_; }

private:
    ChannelQoS qos_;
};

} // namespace wxz::framework
