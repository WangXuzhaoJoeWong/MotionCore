#pragma once

#include <chrono>

#include "framework/node.h"
#include "framework/time.h"

namespace wxz::framework {

/// ROS2-like spin_once：
/// - 统一驱动 NodeBase tick + framework timers + executor 队列
/// - 线程模型：由调用方所在线程执行（通常是主循环线程）
///
/// 返回值：是否执行了至少一个 executor task。
/// 注意：
/// - 这里不会 sleep；如果你想要固定频率循环，请配合 Rate 使用。
template <class Rep, class Period>
bool spin_once(Node& node, const std::chrono::duration<Rep, Period>& timeout) {
    node.base().tick();
    node.tick_timers();
    return node.executor().spin_once(timeout);
}

/// 不带等待的 spin_once（timeout=0）。
inline bool spin_some(Node& node) {
    return spin_once(node, std::chrono::milliseconds(0));
}

/// ROS2-like spin：阻塞循环，直到 node.base().running()==false。
/// - slice: 单次 executor 等待/处理时间片
/// - loop_period: 主循环周期（用于 sleep，避免空转）
inline void spin(Node& node,
                 std::chrono::milliseconds slice = std::chrono::milliseconds(10),
                 std::chrono::milliseconds loop_period = std::chrono::milliseconds(10)) {
    Rate rate(loop_period);
    while (node.base().running()) {
        (void)spin_once(node, slice);
        (void)rate.sleep();
    }
}

} // namespace wxz::framework
