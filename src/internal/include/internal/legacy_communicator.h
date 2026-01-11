#ifndef MOTIONCORE_INTERNAL_LEGACY_COMMUNICATOR_H
#define MOTIONCORE_INTERNAL_LEGACY_COMMUNICATOR_H

// NOTE (legacy/internal):
// This header defines the historical ICommunicator/FastDDSCommunicator abstraction.
// It is retained only for platform-internal legacy wire protocols (e.g. internal ParamServer)
// and a small set of compatibility/regression tests.
//
// New business/service code MUST use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}.
// See docs/ref/推荐用法-P0-通信抽象.md.
//
// Hardening switches for downstream/business code:
// - Define WXZ_FORBID_LEGACY_COMMUNICATION=1 to make including this header a compile-time error.
// - Define WXZ_DEPRECATE_LEGACY_COMMUNICATION=1 to mark legacy types with [[deprecated]] warnings.
//
// Platform-internal boundary enforcement:
// - Define WXZ_ENFORCE_LEGACY_COMMUNICATION_INTERNAL_ONLY=1 to require an explicit allow-list macro
//   (WXZ_LEGACY_COMMUNICATION_ALLOWED=1) for any translation unit that includes this header.
//   This is intended to keep legacy usage confined to platform internal code and tests.

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
    std::string reliability{"reliable"};    // reliable | best_effort
    std::string history{"keep_last"};       // keep_last | keep_all
    int depth{8};                            // used when keep_last
    std::string durability{"volatile"};     // volatile | transient_local
    std::optional<int64_t> deadline_ns;
    std::optional<int64_t> latency_budget_ns;
    std::optional<int64_t> liveliness_lease_ns;
    std::string liveliness{"automatic"};    // automatic | manual_by_topic
    std::string ownership{"shared"};        // shared | exclusive
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
