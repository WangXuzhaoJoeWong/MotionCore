#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "dto/heartbeat_dto.h"
#include "dto/heartbeat_dto_cdr.h"
#include "capability_status.h"
#include "fault_status.h"
#include "fastdds_channel.h"
#include "service_common.h"
#include "clock.h"
#include "time_sync.h"

namespace wxz::core {

struct NodeBaseConfig {
    std::string service;
    std::string type;

    // 可选：capability/status 的版本信息
    std::string version;   // 自由文本，可选
    int api_version{1};
    int schema_version{1};

    int domain{0};

    // 可选：可观测性
    std::string health_file;                 // 为空 => 禁用
    std::string capability_topic;            // 为空 => 禁用
    std::string fault_topic;                 // 为空 => 禁用（fault/status）
    std::string heartbeat_topic;             // 为空 => 禁用（heartbeat/status）
    int health_period_ms{1000};
    int capability_period_ms{1000};
    int heartbeat_period_ms{1000};

    // 可选：时间同步健康探测（NTP/PTP）。
    // - 0 => 禁用
    // - >0 => 按周期调用 timesync probe，并输出 metrics；不同实例可用 scope 区分。
    int timesync_period_ms{0};
    std::string timesync_scope; // 为空 => 默认用 service

    // capability/status payload 字段
    std::vector<std::string> topics_pub;
    std::vector<std::string> topics_sub;

    // 可选：告警输出（例如 logger）
    std::function<void(const std::string&)> warn;
};

// 最小化的进程/节点生命周期辅助：
// - 安装 SIGINT/SIGTERM 信号处理（默认假设每个进程只有一个实例）
// - 周期写入 health 文件并发布 capability/status
class NodeBase {
public:
    using Clock = std::chrono::steady_clock;

    explicit NodeBase(NodeBaseConfig cfg)
        : cfg_(std::move(cfg)),
                    last_health_(clock_steady_now() - std::chrono::seconds(10)),
                    last_capability_(clock_steady_now() - std::chrono::seconds(10)),
                    last_heartbeat_(clock_steady_now() - std::chrono::seconds(10)),
                    last_timesync_(clock_steady_now() - std::chrono::seconds(10)) {
        if (!cfg_.capability_topic.empty()) {
            capability_pub_.emplace(cfg_.domain, cfg_.capability_topic, default_reliable_qos(), 2048);
        }
        if (!cfg_.fault_topic.empty()) {
            fault_pub_.emplace(cfg_.domain, cfg_.fault_topic, default_reliable_qos(), 2048);
        }
        if (!cfg_.heartbeat_topic.empty()) {
            heartbeat_pub_.emplace(cfg_.domain, cfg_.heartbeat_topic, default_reliable_qos(), 2048);
        }
    }

    ~NodeBase() {
        if (g_running_ptr_ == &running_) g_running_ptr_ = nullptr;
    }

    void install_signal_handlers() {
        g_running_ptr_ = &running_;
        std::signal(SIGINT, &NodeBase::on_signal);
        std::signal(SIGTERM, &NodeBase::on_signal);
    }

    bool running() const { return running_.load(); }

    void request_stop() { running_.store(false); }

    int domain() const { return cfg_.domain; }

    bool publish_fault(FaultStatus st) {
        if (!fault_pub_) return false;
        st.domain = cfg_.domain;
        if (st.service.empty()) st.service = cfg_.service;
        if (st.api_version == 0) st.api_version = cfg_.api_version;
        if (st.schema_version == 0) st.schema_version = cfg_.schema_version;
        if (st.version.empty()) st.version = cfg_.version;
        const std::string payload = build_fault_status_payload(st);
        return fault_pub_->publish(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
    }

    void tick() {
        const auto now = clock_steady_now();

        if (cfg_.timesync_period_ms > 0) {
            if (elapsed_ms(now, last_timesync_) >= cfg_.timesync_period_ms) {
                const TimeSyncStatus st = probe_timesync();
                const std::string_view scope = cfg_.timesync_scope.empty() ? std::string_view(cfg_.service)
                                                                          : std::string_view(cfg_.timesync_scope);
                publish_timesync_metrics(st, scope);
                if (!st.synced) {
                    warn("timesync not synced (source=" + st.source + ")");
                }
                last_timesync_ = now;
            }
        }

        if (!cfg_.health_file.empty()) {
            if (elapsed_ms(now, last_health_) >= cfg_.health_period_ms) {
                const bool ok = write_health_file(cfg_.health_file, cfg_.service, true);
                if (!ok) warn("health file write failed: '" + cfg_.health_file + "'");
                last_health_ = now;
            }
        }

        if (capability_pub_) {
            if (elapsed_ms(now, last_capability_) >= cfg_.capability_period_ms) {
                CapabilityStatus st;
                st.service = cfg_.service;
                st.type = cfg_.type;
                st.version = cfg_.version;
                st.api_version = cfg_.api_version;
                st.schema_version = cfg_.schema_version;
                st.domain = cfg_.domain;
                st.ok = true;
                st.topics_pub = cfg_.topics_pub;
                st.topics_sub = cfg_.topics_sub;
                const std::string payload = build_capability_payload(st);
                const bool ok = capability_pub_->publish(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
                if (!ok) warn("capability publish failed");
                last_capability_ = now;
            }
        }

        if (heartbeat_pub_) {
            if (elapsed_ms(now, last_heartbeat_) >= cfg_.heartbeat_period_ms) {
                HeartbeatDTO hb;
                hb.version = 1;
                hb.node = cfg_.service;
                hb.timestamp = now_epoch_ms();
                hb.state = 1; // HEALTHY
                hb.message = cfg_.type;

                std::vector<std::uint8_t> payload;
                const bool encoded = wxz::dto::encode_heartbeat_dto_cdr(hb, payload, /*initial_reserve=*/512);
                const bool ok = encoded && !payload.empty() &&
                                heartbeat_pub_->publish(payload.data(), payload.size());
                if (!ok) warn("heartbeat publish failed");
                last_heartbeat_ = now;
            }
        }
    }

    // 分段 sleep，保证 stop 信号能尽快生效。
    bool sleep_for(std::chrono::milliseconds dur, std::chrono::milliseconds quantum = std::chrono::milliseconds(50)) {
        auto remaining = dur;
        while (running() && remaining.count() > 0) {
            const auto step = (remaining > quantum) ? quantum : remaining;
            std::this_thread::sleep_for(step);
            remaining -= step;
        }
        return running();
    }

private:
    static inline std::atomic<bool>* g_running_ptr_{nullptr};

    static void on_signal(int) {
        if (g_running_ptr_) g_running_ptr_->store(false);
    }

    static inline int elapsed_ms(const Clock::time_point& a, const Clock::time_point& b) {
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(a - b).count());
    }

    void warn(const std::string& msg) {
        if (cfg_.warn) cfg_.warn(msg);
    }

    NodeBaseConfig cfg_;
    std::atomic<bool> running_{true};

    Clock::time_point last_health_;
    Clock::time_point last_capability_;
    Clock::time_point last_heartbeat_;
    Clock::time_point last_timesync_;

    std::optional<FastddsChannel> capability_pub_;
    std::optional<FastddsChannel> fault_pub_;
    std::optional<FastddsChannel> heartbeat_pub_;
};

} // namespace wxz::core
