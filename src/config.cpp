#include "internal/config.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <yaml-cpp/yaml.h>
#include "inproc_channel.h"

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

Config::Config() {
    // 尝试读取本地 YAML 格式的配置文件，路径：`./config/wxz_config.yaml`
    // 在持久部署中，本地 YAML 配置优先于环境变量
    try {
        const char* override_path = std::getenv("WXZ_CONFIG_PATH");
        const std::string cfg_path = (override_path && *override_path) ? std::string(override_path)
                                                                       : std::string("config/wxz_config.yaml");
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path cfg_p(cfg_path);
        // 归一化为绝对路径，保证相对路径解析的一致性。
        const fs::path abs_cfg_p = fs::absolute(cfg_p, ec);
        if (!ec) cfg_p = abs_cfg_p;
        config_path_ = cfg_p.string();
        config_dir_ = cfg_p.parent_path().string();

        YAML::Node doc = YAML::LoadFile(config_path_);
        if (doc && doc["threading"]) {
            for (auto it = doc["threading"].begin(); it != doc["threading"].end(); ++it) {
                std::string module = it->first.as<std::string>();
                YAML::Node node = it->second;
                if (node && node["threads"]) {
                    try {
                        int n = node["threads"].as<int>();
                        if (n > 0) thread_counts_[module] = n;
                    } catch (...) {}
                }
            }
        }
        if (doc && doc["comm"]) {
            if (doc["comm"]["type"]) {
                std::string t = doc["comm"]["type"].as<std::string>();
                // 归一化为大写
                std::string up;
                up.reserve(t.size());
                for (char c : t) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
                if (up == "FASTDDS") {
                    comm_type_ = up;
                }
            }
        }

        // fastdds（可选，通常由运维侧管理）
        // 示例：
        // fastdds:
        //   environment_file: resources/fastdds_peers_example.xml
        //   log_filename: /tmp/fastdds.log
        //   verbosity: warning
        if (doc && doc["fastdds"]) {
            auto f = doc["fastdds"];

            auto resolve_file = [this](const std::string& p) -> std::string {
                if (p.empty()) return p;
                namespace fs = std::filesystem;
                fs::path fp(p);
                if (fp.is_relative()) {
                    std::error_code ec;
                    fs::path base(config_dir_.empty() ? "." : config_dir_);
                    fs::path combined = (base / fp).lexically_normal();
                    fs::path abs = fs::absolute(combined, ec);
                    return (ec ? combined.string() : abs.lexically_normal().string());
                }
                return fp.string();
            };

            if (f["environment_file"]) {
                fastdds_environment_file_ = resolve_file(f["environment_file"].as<std::string>(""));
            }
            if (f["log_filename"]) {
                fastdds_log_filename_ = f["log_filename"].as<std::string>("");
            }
            if (f["verbosity"]) {
                fastdds_verbosity_ = f["verbosity"].as<std::string>("");
            }

            auto set_if_unset = [](const char* key, const std::string& value) {
                if (value.empty()) return;
                const char* cur = std::getenv(key);
                if (!cur || !*cur) {
                    setenv(key, value.c_str(), 1);
                }
            };

            // 必须在创建任何 participant 之前设置。
            set_if_unset("FASTDDS_ENVIRONMENT_FILE", fastdds_environment_file_);
            set_if_unset("FASTDDS_LOG_FILENAME", fastdds_log_filename_);
            set_if_unset("FASTDDS_VERBOSITY", fastdds_verbosity_);
        }
        if (doc && doc["param_server"]) {
            if (doc["param_server"]["enable"]) {
                param_server_enable_ = doc["param_server"]["enable"].as<bool>();
            }
            if (doc["param_server"]["set_topic"]) {
                param_set_topic_ = doc["param_server"]["set_topic"].as<std::string>();
            }
            if (doc["param_server"]["ack_topic"]) {
                param_ack_topic_ = doc["param_server"]["ack_topic"].as<std::string>();
            }
        }

        // discovery / heartbeat（发现 / 心跳）
        if (doc && doc["discovery"]) {
            if (doc["discovery"]["endpoint"]) {
                discovery_endpoint_ = doc["discovery"]["endpoint"].as<std::string>("");
            }
            if (doc["discovery"]["heartbeat_period_ms"]) {
                heartbeat_period_ms_ = doc["discovery"]["heartbeat_period_ms"].as<int>(0);
            }
            // 前/后向兼容的字段命名：
            // - 标准字段：ttl_ms
            // - 别名（历史文档）：heartbeat_ttl_ms
            if (doc["discovery"]["ttl_ms"]) {
                heartbeat_ttl_ms_ = doc["discovery"]["ttl_ms"].as<int>(0);
            } else if (doc["discovery"]["heartbeat_ttl_ms"]) {
                heartbeat_ttl_ms_ = doc["discovery"]["heartbeat_ttl_ms"].as<int>(0);
            }
            if (doc["discovery"]["node_role"]) {
                node_role_ = doc["discovery"]["node_role"].as<std::string>("");
            }
            if (doc["discovery"]["zone"]) {
                node_zone_ = doc["discovery"]["zone"].as<std::string>("");
            }
            if (doc["discovery"]["node_endpoints"]) {
                for (const auto& ep : doc["discovery"]["node_endpoints"]) {
                    node_endpoints_.push_back(ep.as<std::string>(""));
                }
            }
        }

        // event queue / dispatcher（事件队列 / 分发器）
        if (doc && doc["queue"]) {
            if (doc["queue"]["max_size"]) {
                event_queue_max_size_ = doc["queue"]["max_size"].as<int>(event_queue_max_size_);
            }
            if (doc["queue"]["high_watermark"]) {
                event_queue_high_watermark_ = doc["queue"]["high_watermark"].as<int>(event_queue_high_watermark_);
            }
            if (doc["queue"]["block_when_full"]) {
                event_queue_block_when_full_ = doc["queue"]["block_when_full"].as<bool>(event_queue_block_when_full_);
            }
            if (doc["queue"]["drop_oldest"]) {
                event_queue_drop_oldest_ = doc["queue"]["drop_oldest"].as<bool>(event_queue_drop_oldest_);
            }
        }
        if (doc && doc["dispatcher"]) {
            if (doc["dispatcher"]["max_retries"]) {
                dispatcher_max_retries_ = doc["dispatcher"]["max_retries"].as<int>(dispatcher_max_retries_);
            }
        }

        if (doc && doc["realtime_mode"]) {
            realtime_mode_ = doc["realtime_mode"].as<bool>(realtime_mode_);
        }

        // observability（可观测性，可选）
        // 示例：
        // metrics:
        //   period_ms: 5000
        if (doc && doc["metrics"]) {
            auto m = doc["metrics"];
            if (m["period_ms"]) {
                metrics_period_ms_ = m["period_ms"].as<int>(metrics_period_ms_);
            }
        }

        // fault recovery executor（故障恢复执行器，可选）
        // 示例：
        // fault_recovery:
        //   enable: true
        //   topic: fault/status
        //   rules:
        //     - match: { fault: demo.degrade }
        //       action: degrade
        //       marker_file: /tmp/wxz_degraded
        //     - match: { fault: demo.restart }
        //       action: restart
        //       exit_code: 42
        if (doc && doc["fault_recovery"]) {
            auto fr = doc["fault_recovery"];
            if (fr["enable"]) {
                fault_recovery_enable_ = fr["enable"].as<bool>(fault_recovery_enable_);
            }
            if (fr["topic"]) {
                fault_recovery_topic_ = fr["topic"].as<std::string>(fault_recovery_topic_);
            }

            fault_recovery_rules_.clear();
            if (fr["rules"] && fr["rules"].IsSequence()) {
                for (const auto& r : fr["rules"]) {
                    FaultRecoveryRuleConfig rc;
                    if (r["action"]) rc.action = r["action"].as<std::string>(rc.action);
                    if (r["exit_code"]) rc.exit_code = r["exit_code"].as<int>(rc.exit_code);
                    if (r["marker_file"]) rc.marker_file = r["marker_file"].as<std::string>(rc.marker_file);

                    if (r["match"]) {
                        auto m = r["match"];
                        if (m["fault"]) rc.fault = m["fault"].as<std::string>(rc.fault);
                        if (m["service"]) rc.service = m["service"].as<std::string>(rc.service);
                        if (m["severity"]) rc.severity = m["severity"].as<std::string>(rc.severity);
                    }

                    // 只接受已知 action
                    if (rc.action == "restart" || rc.action == "degrade") {
                        fault_recovery_rules_.push_back(std::move(rc));
                    }
                }
            }
        }

        // channels（FastDDS / 其它传输）
        if (doc && doc["channels"]) {
            for (auto it = doc["channels"].begin(); it != doc["channels"].end(); ++it) {
                ChannelConfig cfg;
                cfg.name = it->first.as<std::string>();
                YAML::Node n = it->second;
                if (n["transport"]) cfg.transport = n["transport"].as<std::string>(cfg.transport);
                if (n["domain"]) cfg.domain = n["domain"].as<int>(cfg.domain);
                if (n["topic"]) cfg.topic = n["topic"].as<std::string>(cfg.topic);
                if (n["max_payload"]) cfg.max_payload = n["max_payload"].as<std::size_t>(cfg.max_payload);
                if (n["locators"]) {
                    for (const auto& l : n["locators"]) {
                        cfg.locators.push_back(l.as<std::string>(""));
                    }
                }

                // shm 传输参数
                // 示例：
                // channels:
                //   cam_req:
                //     transport: shm
                //     shm:
                //       name: /wxz_camera_req
                //       capacity: 64
                //       slot_size: 65536
                if (n["shm"]) {
                    auto s = n["shm"];
                    if (s["name"]) cfg.shm_name = s["name"].as<std::string>(cfg.shm_name);
                    if (s["capacity"]) cfg.shm_capacity = s["capacity"].as<std::size_t>(cfg.shm_capacity);
                    if (s["slot_size"]) cfg.shm_slot_size = s["slot_size"].as<std::size_t>(cfg.shm_slot_size);
                }

                if (n["qos"]) {
                    YAML::Node q = n["qos"];
                    auto to_upper = [](std::string s) {
                        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                        return s;
                    };
                    if (q["reliability"]) {
                        auto v = to_upper(q["reliability"].as<std::string>("RELIABLE"));
                        cfg.qos.reliability = (v == "BEST_EFFORT") ? ChannelQoS::Reliability::best_effort : ChannelQoS::Reliability::reliable;
                    }
                    if (q["history"]) {
                        auto v = to_upper(q["history"].as<std::string>("KEEP_LAST"));
                        if (v == "KEEP_ALL") cfg.qos.history = 0; // keep_all（保留全部历史）
                        else if (q["depth"]) cfg.qos.history = q["depth"].as<std::size_t>(cfg.qos.history);
                    }
                    if (q["depth"]) cfg.qos.history = q["depth"].as<std::size_t>(cfg.qos.history);
                    if (q["deadline_ns"]) cfg.qos.deadline_ns = q["deadline_ns"].as<std::uint64_t>(cfg.qos.deadline_ns);
                    if (q["latency_budget_ns"]) cfg.qos.latency_budget_ns = q["latency_budget_ns"].as<std::uint64_t>(cfg.qos.latency_budget_ns);
                    if (q["lifespan_ns"]) cfg.qos.lifespan_ns = q["lifespan_ns"].as<std::uint64_t>(cfg.qos.lifespan_ns);
                    if (q["time_based_filter_ns"]) cfg.qos.time_based_filter_ns = q["time_based_filter_ns"].as<std::uint64_t>(cfg.qos.time_based_filter_ns);
                    if (q["durability"]) {
                        auto v = to_upper(q["durability"].as<std::string>("VOLATILE_KIND"));
                        cfg.qos.durability = (v == "TRANSIENT_LOCAL") ? ChannelQoS::Durability::transient_local : ChannelQoS::Durability::volatile_kind;
                    }
                    if (q["liveliness"]) {
                        auto v = to_upper(q["liveliness"].as<std::string>("AUTOMATIC"));
                        cfg.qos.liveliness = (v == "MANUAL_BY_TOPIC") ? ChannelQoS::Liveliness::manual_by_topic : ChannelQoS::Liveliness::automatic;
                    }
                    if (q["ownership"]) {
                        auto v = to_upper(q["ownership"].as<std::string>("SHARED"));
                        cfg.qos.ownership = (v == "EXCLUSIVE") ? ChannelQoS::Ownership::exclusive : ChannelQoS::Ownership::shared;
                    }
                    if (q["ownership_strength"]) cfg.qos.ownership_strength = q["ownership_strength"].as<std::int32_t>(cfg.qos.ownership_strength);
                    if (q["transport_priority"]) cfg.qos.transport_priority = q["transport_priority"].as<std::int32_t>(cfg.qos.transport_priority);
                    if (q["async_publish"]) cfg.qos.async_publish = q["async_publish"].as<bool>(cfg.qos.async_publish);
                    if (q["realtime_hint"]) cfg.qos.realtime_hint = q["realtime_hint"].as<bool>(cfg.qos.realtime_hint);
                }

                channels_[cfg.name] = cfg;
            }
        }

        // channel 治理：按名称 allowlist / denylist
        if (doc && doc["channel_filters"]) {
            auto filters = doc["channel_filters"];
            if (filters["allow"]) {
                for (const auto& n : filters["allow"]) {
                    channel_allowlist_.push_back(n.as<std::string>(""));
                }
            }
            if (filters["deny"]) {
                for (const auto& n : filters["deny"]) {
                    channel_denylist_.push_back(n.as<std::string>(""));
                }
            }
        }
    } catch (...) {
        // 忽略文件缺失或解析错误，保留默认值
    }

    // 环境变量覆盖（用于快速联调）
    if (const char* c = std::getenv("WXZ_COMM_TYPE")) {
        std::string up;
        for (char ch : std::string(c)) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        if (up == "FASTDDS") comm_type_ = up;
    }
    if (const char* ps = std::getenv("WXZ_PARAM_SERVER")) {
        std::string v(ps);
        if (v == "0" || v == "false" || v == "FALSE") param_server_enable_ = false;
    }
    if (const char* st = std::getenv("WXZ_PARAM_SET_TOPIC")) {
        param_set_topic_ = st;
    }
    if (const char* at = std::getenv("WXZ_PARAM_ACK_TOPIC")) {
        param_ack_topic_ = at;
    }
    if (const char* dz = std::getenv("WXZ_DISCOVERY_ZONE")) {
        node_zone_ = dz;
    }

    if (const char* v = std::getenv("WXZ_METRICS_PERIOD_MS")) {
        try { metrics_period_ms_ = std::stoi(v); } catch (...) {}
    }

    if (metrics_period_ms_ <= 0) metrics_period_ms_ = 5000;

    // 通过环境变量覆盖 queue / dispatcher 参数
    if (const char* v = std::getenv("WXZ_QUEUE_MAX")) {
        try { event_queue_max_size_ = std::stoi(v); } catch (...) {}
    }
    if (const char* v = std::getenv("WXZ_QUEUE_HWM")) {
        try { event_queue_high_watermark_ = std::stoi(v); } catch (...) {}
    }
    if (const char* v = std::getenv("WXZ_QUEUE_BLOCK")) {
        std::string s(v); std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "0" || s == "false") event_queue_block_when_full_ = false;
        if (s == "1" || s == "true") event_queue_block_when_full_ = true;
    }
    if (const char* v = std::getenv("WXZ_QUEUE_DROP_OLDEST")) {
        std::string s(v); std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "0" || s == "false") event_queue_drop_oldest_ = false;
        if (s == "1" || s == "true") event_queue_drop_oldest_ = true;
    }
    if (const char* v = std::getenv("WXZ_DISPATCHER_MAX_RETRIES")) {
        try { dispatcher_max_retries_ = std::stoi(v); } catch (...) {}
    }
}

int Config::getThreadCount(const std::string &module, int default_n, int max_n) const {
    // 优先级：本地配置 (`thread_counts_`) > 环境变量覆盖 > 默认值
    auto it = thread_counts_.find(module);
    if (it != thread_counts_.end()) {
        return std::min(it->second, max_n);
    }
    // 环境变量覆盖格式：`WXZ_THREADS_<MODULE_UPPER>`
    std::string env_name = "WXZ_THREADS_";
    for (char c : module) {
        env_name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (const char* v = std::getenv(env_name.c_str())) {
        try {
            int val = std::stoi(v);
            if (val <= 0) return default_n;
            return std::min(val, max_n);
        } catch (...) {}
    }
    return default_n;
}

std::string Config::getCommType() const {
    return comm_type_;
}

bool Config::isMultithreaded() const {
    return multithreaded_;
}

bool Config::isParamServerEnabled() const {
    return param_server_enable_;
}

std::string Config::getParamSetTopic() const {
    return param_set_topic_;
}

std::string Config::getParamAckTopic() const {
    return param_ack_topic_;
}

std::string Config::getDiscoveryEndpoint() const {
    return discovery_endpoint_;
}

int Config::getHeartbeatPeriodMs() const {
    return heartbeat_period_ms_;
}

int Config::getHeartbeatTtlMs() const {
    return heartbeat_ttl_ms_;
}

std::string Config::getNodeRole() const {
    return node_role_;
}

std::string Config::getNodeZone() const {
    return node_zone_;
}

const std::vector<std::string>& Config::getNodeEndpoints() const {
    return node_endpoints_;
}

int Config::getEventQueueMaxSize() const { return event_queue_max_size_; }
int Config::getEventQueueHighWatermark() const { return event_queue_high_watermark_; }
bool Config::getEventQueueBlockWhenFull() const { return event_queue_block_when_full_; }
bool Config::getEventQueueDropOldest() const { return event_queue_drop_oldest_; }
int Config::getDispatcherMaxRetries() const { return dispatcher_max_retries_; }
