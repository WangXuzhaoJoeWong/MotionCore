#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

struct PeerInfo {
    std::string endpoint;
    std::string role;
    std::string zone;
    std::string qos;
};

// 简单的基于 HTTP 的发现客户端：发送 register + heartbeat 的 JSON payload。
class DiscoveryClient {
public:
    DiscoveryClient() = default;
    ~DiscoveryClient();

    // 若配置有效则启动 heartbeat 线程。
    void start(const std::string& endpoint,
               int heartbeat_period_ms,
               int ttl_ms,
               const std::string& node_role,
               const std::string& node_zone,
               const std::vector<std::string>& node_endpoints);

    // 停止 heartbeat 线程。
    void stop();

    bool isRunning() const { return running_; }

    // 返回最近一次拉取到的 peers 列表（按 role/zone/qos 过滤后，仅 endpoint）。
    std::vector<std::string> getPeers() const;

    // 返回原始 peer 元数据（不做过滤）。
    std::vector<PeerInfo> getPeerInfos() const;

    // 可选：在 heartbeat 循环之外手动刷新 peers（GET）。
    void refreshPeers();

private:
    void run();
    bool sendRegister();
    bool sendDeregister();
    bool sendHeartbeat();
    bool postJson(const std::string& payload, const char* purpose);
    bool fetchPeers();

    std::string endpoint_;
    int period_ms_{0};
    int ttl_ms_{0};
    std::string node_role_;
    std::string node_zone_;
    std::vector<std::string> node_endpoints_;
    std::string hostname_;

    mutable std::mutex peers_mutex_;
    std::vector<PeerInfo> peer_infos_;

    std::atomic<bool> running_{false};
    std::thread worker_;
};
