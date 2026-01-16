// 内部（wire）ParamServer：基于 FastddsChannel 的轻量运行时参数服务。
//
// 注意：
// - 这里刻意不是对外的 public API。
// - 对外/稳定 API 为 wxz::core::IParamServer 与 wxz::core::{ParamServer,DistributedParamServer}，
//   位于 include/param_server.h。
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
        std::string type;   // 类型："string" | "int" | "double" | "bool"
        bool read_only{false};
    };

    ParamServer(int domain_id,
                std::string set_topic = "param.set",
                std::string ack_topic = "param.ack");
    ~ParamServer();

    // 声明参数：指定默认值与更新回调。
    void declare(const std::string& name, const std::string& default_val, Callback cb);
    // 为参数声明 schema/ACL。
    void setSchema(const std::string& name, ParamSpec spec);

    // 以编程方式批量应用参数（不走 wire）；用于从配置中心引导启动等场景。
    void applyBulk(const std::unordered_map<std::string, std::string>& kvs);

    // 导出全部参数（线程不安全的快照；用于 UI/远程调试暴露）。
    std::unordered_map<std::string, std::string> exportAll() const;
    std::string exportAllJson() const;

    // 配置快照路径并持久化/加载。
    void setSnapshotPath(std::string path);
    void loadSnapshot();
    void saveSnapshot();

    // 配置周期拉取适配器：设置回调，从 Consul/etcd/HTTP 拉取并返回 key/value。
    void setFetchCallback(FetchCallback cb, std::chrono::milliseconds interval);
    // 便捷接口：按给定周期从 HTTP 拉取（key=value 行）。
    void setHttpFetch(const std::string& url, std::chrono::milliseconds interval);
    // 便捷接口：从多个端点周期拉取（合并 key=value 行）。
    void setHttpFetchList(const std::vector<std::string>& urls, std::chrono::milliseconds interval);

    // 可选：暴露 request/reply topic 用于导出全部参数（通过 communicator 的类 RPC 风格）。
    void setExportTopics(std::string request_topic, std::string reply_topic);

    void start();
    void stop();

    // 当 worker 线程至少进入过一次主循环后返回 true。
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

    // 周期拉取适配器
    FetchCallback fetch_cb_;
    std::chrono::milliseconds fetch_interval_{0};
    std::atomic<bool> fetch_running_{false};
    std::thread fetch_thread_;

    std::string export_request_topic_;
    std::string export_reply_topic_;
};

} // namespace wxz::core::internal
