#ifndef MOTIONCORE_INTERNAL_LEGACY_COMMUNICATOR_H
#define MOTIONCORE_INTERNAL_LEGACY_COMMUNICATOR_H

// 注意（legacy/internal）：
// 该头文件定义了历史遗留的 ICommunicator/FastDDSCommunicator 抽象。
// 仅用于平台内部的 legacy wire 协议（例如内部 ParamServer）以及少量兼容/回归测试。
//
// 新的业务/服务代码必须使用 wxz::core::{FastddsChannel,InprocChannel,ShmChannel}。
// 参考 docs/ref/推荐用法-P0-通信抽象.md。
//
// 面向下游/业务代码的加固开关：
// - 定义 WXZ_FORBID_LEGACY_COMMUNICATION=1：包含该头文件会触发编译期错误。
// - 定义 WXZ_DEPRECATE_LEGACY_COMMUNICATION=1：为 legacy 类型增加 [[deprecated]] 警告。
//
// 平台内部边界约束：
// - 定义 WXZ_ENFORCE_LEGACY_COMMUNICATION_INTERNAL_ONLY=1：要求任何包含该头文件的编译单元
//   显式加入 allow-list 宏（WXZ_LEGACY_COMMUNICATION_ALLOWED=1）。
//   该机制用于把 legacy 使用限制在平台内部代码与测试中。

#if defined(WXZ_FORBID_LEGACY_COMMUNICATION)
#error "Legacy ICommunicator/FastDDSCommunicator is forbidden. Use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. See docs/ref/推荐用法-P0-通信抽象.md"
#endif

#if defined(WXZ_ENFORCE_LEGACY_COMMUNICATION_INTERNAL_ONLY) && !defined(WXZ_LEGACY_COMMUNICATION_ALLOWED)
#error "Legacy ICommunicator/FastDDSCommunicator is internal-only in this build. Use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. If you are platform internal code/tests, add WXZ_LEGACY_COMMUNICATION_ALLOWED=1 for that target."
#endif

#if defined(WXZ_DEPRECATE_LEGACY_COMMUNICATION)
    #if defined(__has_cpp_attribute)
        #if __has_cpp_attribute(deprecated)
            #define WXZ_LEGACY_COMM_DEPRECATED(msg) [[deprecated(msg)]]
        #else
            #define WXZ_LEGACY_COMM_DEPRECATED(msg)
        #endif
    #else
        #define WXZ_LEGACY_COMM_DEPRECATED(msg)
    #endif
#else
    #define WXZ_LEGACY_COMM_DEPRECATED(msg)
#endif

#include "dto/event_dto.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

struct TopicQosProfile {
    std::string reliability{"reliable"};    // 可靠性：reliable | best_effort
    std::string history{"keep_last"};       // 历史策略：keep_last | keep_all
    int depth{8};                            // keep_last 时使用
    std::string durability{"volatile"};     // 持久性：volatile | transient_local
    std::optional<int64_t> deadline_ns;
    std::optional<int64_t> latency_budget_ns;
    std::optional<int64_t> liveliness_lease_ns;
    std::string liveliness{"automatic"};    // 活性：automatic | manual_by_topic
    std::string ownership{"shared"};        // 所有权：shared | exclusive
    std::optional<int> ownership_strength;
    std::optional<int64_t> time_based_filter_ns;
    std::optional<int64_t> lifespan_ns;
    std::optional<int> transport_priority;
    std::optional<bool> async_publish;
};

class WXZ_LEGACY_COMM_DEPRECATED(
    "ICommunicator is legacy. Prefer wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. See docs/ref/推荐用法-P0-通信抽象.md")
ICommunicator {
public:
    virtual ~ICommunicator() = default;
    virtual void send(const std::string& topic, const std::string& message) = 0;
    virtual std::string receive(const std::string& topic) = 0;

    virtual void sendDTO(const std::string& topic, const struct EventDTO& dto) = 0;
    virtual bool receiveDTO(const std::string& topic, struct EventDTO& out) = 0;

    virtual void setPeers(const std::vector<std::string>& peers) { (void)peers; }
    virtual void setTopicQos(const std::string& topic, const TopicQosProfile& qos) { (void)topic; (void)qos; }
};

class WXZ_LEGACY_COMM_DEPRECATED(
    "FastDDSCommunicator is legacy. Prefer wxz::core::FastddsChannel. See docs/ref/推荐用法-P0-通信抽象.md")
FastDDSCommunicator : public ICommunicator {
public:
    FastDDSCommunicator();
    ~FastDDSCommunicator();

    void send(const std::string& topic, const std::string& message) override;
    std::string receive(const std::string& topic) override;
    void sendDTO(const std::string& topic, const struct EventDTO& dto) override;
    bool receiveDTO(const std::string& topic, struct EventDTO& out) override;
    void setPeers(const std::vector<std::string>& peers) override;
    void setTopicQos(const std::string& topic, const TopicQosProfile& qos) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // MOTIONCORE_INTERNAL_LEGACY_COMMUNICATOR_H
