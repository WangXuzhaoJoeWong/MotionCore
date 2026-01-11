// Internal (wire) ParamServer: lightweight runtime parameter server over FastddsChannel.
//
// NOTE:
// - This is intentionally NOT the public API.
// - Public/stable API is wxz::core::IParamServer and wxz::core::{ParamServer,DistributedParamServer}
//   in include/param_server.h.
#pragma once

#include "internal/config_fetcher.h"

#include "fastdds_channel.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace wxz::core::internal {

class ParamServer {
public:
    using Callback = std::function<void(const std::string&, const std::string&)>;
    using FetchCallback = std::function<std::unordered_map<std::string, std::string>()>;

    struct ParamSpec {
        std::string type;   // "string" | "int" | "double" | "bool"
        bool read_only{false};
    };

    ParamServer(int domain_id,
                std::string set_topic = "param.set",
                std::string ack_topic = "param.ack");
    ~ParamServer();

    // Declare a parameter with default value and an update callback.
    void declare(const std::string& name, const std::string& default_val, Callback cb);
    // Declare schema/ACL for a parameter.
    void setSchema(const std::string& name, ParamSpec spec);

    // Apply a batch of parameters programmatically (no wire); useful for bootstrapping from config center.
    void applyBulk(const std::unordered_map<std::string, std::string>& kvs);

    // Export all parameters (thread-unsafe snapshot; intended for UI/remote debug exposure).
    std::unordered_map<std::string, std::string> exportAll() const;
    std::string exportAllJson() const;

    // Configure snapshot path and persist/load.
    void setSnapshotPath(std::string path);
    void loadSnapshot();
    void saveSnapshot();

    // Configure periodic fetch adapter; set callback that pulls from Consul/etcd/HTTP and returns key/values.
    void setFetchCallback(FetchCallback cb, std::chrono::milliseconds interval);
    // Convenience: periodic HTTP fetch (key=value lines) with given interval.
    void setHttpFetch(const std::string& url, std::chrono::milliseconds interval);
    // Convenience: periodic HTTP fetch from multiple endpoints (merged key=value lines).
    void setHttpFetchList(const std::vector<std::string>& urls, std::chrono::milliseconds interval);

    // Optional: expose a request/reply topic to export all params (RPC-style over communicator).
    void setExportTopics(std::string request_topic, std::string reply_topic);

    void start();
    void stop();

    // True after the worker thread has entered the main loop at least once.
    bool hasEnteredLoop() const { return loop_entered_.load(); }

private:
    void loop();

    void maybeStartFetchThread();

    void handleSetMessage(const std::string& msg);
    void handleBulkMessage(const std::string& body);
    bool validateAndApply(const std::string& key, const std::string& val, bool send_ack);
    bool typeAccepts(const std::string& type, const std::string& val) const;
    void sendAckOk(const std::string& key, const std::string& val);
    void sendAckError(const std::string& key, const std::string& err);
    void persistIfConfigured();

    void fetchLoop();

    void ensureChannelsStarted();
    void ensureExportChannelsStarted();
    void publishOnAckTopic(const std::string& payload);
    void publishOnExportReplyTopic(const std::string& payload);

    int domain_id_{0};
    wxz::core::ChannelQoS qos_{};
    std::size_t max_payload_{65536};

    std::mutex channel_mu_;
    std::unique_ptr<wxz::core::FastddsChannel> set_sub_;
    wxz::core::Subscription set_subscription_;
    std::unique_ptr<wxz::core::FastddsChannel> ack_pub_;

    std::unique_ptr<wxz::core::FastddsChannel> export_req_sub_;
    wxz::core::Subscription export_subscription_;
    std::unique_ptr<wxz::core::FastddsChannel> export_reply_pub_;

    std::mutex queue_mu_;
    std::condition_variable queue_cv_;
    std::deque<std::string> set_queue_;
    std::deque<std::string> export_queue_;

    std::unordered_map<std::string, std::string> params_;
    std::unordered_map<std::string, Callback> callbacks_;
    std::unordered_map<std::string, ParamSpec> schemas_;
    std::string snapshot_path_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::string set_topic_;
    std::string ack_topic_;

    std::atomic<bool> loop_entered_{false};

    // Periodic fetch adapter
    FetchCallback fetch_cb_;
    std::chrono::milliseconds fetch_interval_{0};
    std::atomic<bool> fetch_running_{false};
    std::thread fetch_thread_;

    std::string export_request_topic_;
    std::string export_reply_topic_;
};

} // namespace wxz::core::internal
