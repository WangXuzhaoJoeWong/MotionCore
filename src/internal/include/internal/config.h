#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>
#include <vector>

namespace wxz {
inline constexpr const char* kWxzVersion = "1.2.0";
}

#include "inproc_channel.h" // for ChannelQoS

using wxz::core::ChannelQoS;

struct ChannelConfig {
    std::string name;
    std::string transport{"fastdds"};
    int domain{0};
    std::string topic;
    ChannelQoS qos{};
    std::size_t max_payload{4096};
    std::vector<std::string> locators; // reserved

    // shm transport (optional)
    // When transport == "shm", use shm.name/capacity/slot_size to configure ShmChannel.
    std::string shm_name;
    std::size_t shm_capacity{0};
    std::size_t shm_slot_size{0};
};

struct FaultRecoveryRuleConfig {
    std::string fault;       // match: fault id (optional)
    std::string service;     // match: reporter service (optional)
    std::string severity;    // match: info|warn|error|fatal (optional)
    std::string action;      // restart|degrade
    int exit_code{42};       // for restart
    std::string marker_file; // for degrade
};

// 配置（单例）
class Config {
public:
    static Config& getInstance();
    // Resolved path of the YAML used to initialize this Config (set via WXZ_CONFIG_PATH or default).
    const std::string& getConfigPath() const { return config_path_; }
    // Directory containing the YAML config (derived from getConfigPath()).
    const std::string& getConfigDir() const { return config_dir_; }
    std::string getCommType() const;
    bool isMultithreaded() const;

    // 线程配置辅助：根据模块名（例如 `taskflow`）获取线程数。
    // 如果未配置或值非法则返回 `default_n`；返回值不会超过 `max_n`。
    int getThreadCount(const std::string &module, int default_n, int max_n) const;

    // 参数服务器
    bool isParamServerEnabled() const;
    std::string getParamSetTopic() const;
    std::string getParamAckTopic() const;

    // 服务发现 / 心跳
    std::string getDiscoveryEndpoint() const;
    int getHeartbeatPeriodMs() const;
    int getHeartbeatTtlMs() const;
    std::string getNodeRole() const;
    std::string getNodeZone() const;
    const std::vector<std::string>& getNodeEndpoints() const;

    // 事件与调度配置
    int getEventQueueMaxSize() const;
    int getEventQueueHighWatermark() const;
    bool getEventQueueBlockWhenFull() const;
    bool getEventQueueDropOldest() const;
    int getDispatcherMaxRetries() const;

    bool isRealtimeMode() const { return realtime_mode_; }

    // Channel registry (config-driven)
    const std::map<std::string, ChannelConfig>& getChannels() const { return channels_; }
    const std::vector<std::string>& getChannelAllowlist() const { return channel_allowlist_; }
    const std::vector<std::string>& getChannelDenylist() const { return channel_denylist_; }

    // FastDDS runtime profiles (optional; typically managed by ops)
    const std::string& getFastddsEnvironmentFile() const { return fastdds_environment_file_; }
    const std::string& getFastddsLogFilename() const { return fastdds_log_filename_; }
    const std::string& getFastddsVerbosity() const { return fastdds_verbosity_; }

    // Observability (optional)
    int getMetricsPeriodMs() const { return metrics_period_ms_; }

    // Fault recovery (optional)
    bool isFaultRecoveryEnabled() const { return fault_recovery_enable_; }
    const std::string& getFaultRecoveryTopic() const { return fault_recovery_topic_; }
    const std::vector<FaultRecoveryRuleConfig>& getFaultRecoveryRules() const { return fault_recovery_rules_; }

private:
    Config();
    std::string config_path_{};
    std::string config_dir_{};
    std::string comm_type_ = "FASTDDS";
    bool multithreaded_ = true;
    bool param_server_enable_ = true;
    std::string param_set_topic_ = "param.set";
    std::string param_ack_topic_ = "param.ack";
    // discovery
    std::string discovery_endpoint_ = "";
    int heartbeat_period_ms_{0};
    int heartbeat_ttl_ms_{0};
    std::string node_role_ = "";
    std::string node_zone_ = "";
    std::vector<std::string> node_endpoints_;
    // 事件与调度配置
    int event_queue_max_size_{1024};
    int event_queue_high_watermark_{900};
    bool event_queue_block_when_full_{true};
    bool event_queue_drop_oldest_{true};
    int dispatcher_max_retries_{2};
    bool realtime_mode_{false};
    // Channel registry loaded from config
    std::map<std::string, ChannelConfig> channels_;
    // Governance: optional allow/deny lists for channel names
    std::vector<std::string> channel_allowlist_;
    std::vector<std::string> channel_denylist_;
    // 每个模块的线程数，从本地配置加载
    std::map<std::string, int> thread_counts_;

    // fastdds profiles (mirrors environment variables; empty means "not configured here")
    std::string fastdds_environment_file_{};
    std::string fastdds_log_filename_{};
    std::string fastdds_verbosity_{};

    // Observability
    int metrics_period_ms_{5000};

    // Fault recovery
    bool fault_recovery_enable_{false};
    std::string fault_recovery_topic_{"fault/status"};
    std::vector<FaultRecoveryRuleConfig> fault_recovery_rules_;
};

#endif // CONFIG_H
