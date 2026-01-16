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
    std::vector<std::string> locators; // 预留

    // shm 传输（可选）
    // 当 transport == "shm" 时，使用 shm.name/capacity/slot_size 来配置 ShmChannel。
    std::string shm_name;
    std::size_t shm_capacity{0};
    std::size_t shm_slot_size{0};
};

struct FaultRecoveryRuleConfig {
    std::string fault;       // match：故障 id（可选）
    std::string service;     // match：上报服务名（可选）
    std::string severity;    // match：info|warn|error|fatal（可选）
    std::string action;      // restart|degrade
    int exit_code{42};       // 用于 restart
    std::string marker_file; // 用于 degrade
};

// 配置（单例）
class Config {
public:
    static Config& getInstance();
    // 初始化该 Config 所使用的 YAML 路径（由 WXZ_CONFIG_PATH 指定或使用默认值），并已解析为最终路径。
    const std::string& getConfigPath() const { return config_path_; }
    // YAML 配置所在目录（由 getConfigPath() 推导）。
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

    // Channel 注册表（由配置驱动）
    const std::map<std::string, ChannelConfig>& getChannels() const { return channels_; }
    const std::vector<std::string>& getChannelAllowlist() const { return channel_allowlist_; }
    const std::vector<std::string>& getChannelDenylist() const { return channel_denylist_; }

    // FastDDS 运行时 profiles（可选；通常由运维侧管理）
    const std::string& getFastddsEnvironmentFile() const { return fastdds_environment_file_; }
    const std::string& getFastddsLogFilename() const { return fastdds_log_filename_; }
    const std::string& getFastddsVerbosity() const { return fastdds_verbosity_; }

    // 可观测性（可选）
    int getMetricsPeriodMs() const { return metrics_period_ms_; }

    // 故障恢复（可选）
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
    // discovery（发现）
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
    // 从配置加载的 Channel 注册表
    std::map<std::string, ChannelConfig> channels_;
    // 治理：可选的 channel 名称 allow/deny 列表
    std::vector<std::string> channel_allowlist_;
    std::vector<std::string> channel_denylist_;
    // 每个模块的线程数，从本地配置加载
    std::map<std::string, int> thread_counts_;

    // fastdds profiles（镜像环境变量；为空表示“未在此处配置”）
    std::string fastdds_environment_file_{};
    std::string fastdds_log_filename_{};
    std::string fastdds_verbosity_{};

    // 可观测性
    int metrics_period_ms_{5000};

    // 故障恢复
    bool fault_recovery_enable_{false};
    std::string fault_recovery_topic_{"fault/status"};
    std::vector<FaultRecoveryRuleConfig> fault_recovery_rules_;
};

#endif // CONFIG_H
