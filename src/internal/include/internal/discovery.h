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

// Simple HTTP-based discovery client; sends register + heartbeat JSON payloads.
class DiscoveryClient {
public:
    DiscoveryClient() = default;
    ~DiscoveryClient();

    // Starts heartbeat thread if configuration is valid.
    void start(const std::string& endpoint,
               int heartbeat_period_ms,
               int ttl_ms,
               const std::string& node_role,
               const std::string& node_zone,
               const std::vector<std::string>& node_endpoints);

    // Stops heartbeat thread.
    void stop();

    bool isRunning() const { return running_; }

    // Returns last fetched peers list (endpoints) with role/zone/qos filtering.
    std::vector<std::string> getPeers() const;

    // Returns raw peer metadata (unfiltered).
    std::vector<PeerInfo> getPeerInfos() const;

    // Optional manual refresh of peers (GET) outside the heartbeat loop.
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
