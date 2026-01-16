#pragma once

#include <functional>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wxz::core {

using ParamValue = std::variant<int, double, bool, std::string>;

struct ParamDesc {
    std::string name;
    std::string type;      // int/double/bool/string
    ParamValue default_value;
    std::string schema;    // 可选：JSON schema 片段
    bool read_only{false};
};

class IParamObserver {
public:
    virtual ~IParamObserver() = default;
    virtual void onParamChanged(const std::string& key, const ParamValue& value) = 0;
};

class IParamServer {
public:
    virtual ~IParamServer() = default;
    virtual bool declare(const ParamDesc& desc) = 0;
    virtual std::optional<ParamValue> get(const std::string& key) const = 0;
    virtual bool set(const std::string& key, const ParamValue& value) = 0;
    virtual void subscribe(const std::string& key, IParamObserver* observer) = 0;
};

// 默认的进程内实现。
// 说明：
// - 本实现线程安全。
// - 本实现刻意保持轻量，不依赖分布式配置中心。
class ParamServer final : public IParamServer {
public:
    ParamServer();
    ~ParamServer() override;

    ParamServer(const ParamServer&) = delete;
    ParamServer& operator=(const ParamServer&) = delete;
    ParamServer(ParamServer&&) noexcept;
    ParamServer& operator=(ParamServer&&) noexcept;

    bool declare(const ParamDesc& desc) override;
    std::optional<ParamValue> get(const std::string& key) const override;
    bool set(const std::string& key, const ParamValue& value) override;
    void subscribe(const std::string& key, IParamObserver* observer) override;

    // 可选：持久化快照（key=value 行）。用于确定性启动（确定性引导启动）。
    void setSnapshotPath(std::string path);
    void loadSnapshot();
    void saveSnapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// 分布式参数服务：基于内部 wire 协议 ParamServer 的实现。
// （通过 FastddsChannel 原始字节在 param.set/param.ack topics 上传输）
//
// 这是最小桥接层：让下游代码使用稳定的 IParamServer API，同时平台保持既有 wire 协议不变。
class DistributedParamServer final : public IParamServer {
public:
    using FetchCallback = std::function<std::unordered_map<std::string, std::string>()>;

    explicit DistributedParamServer(std::string set_topic = "param.set", std::string ack_topic = "param.ack");
    ~DistributedParamServer() override;

    DistributedParamServer(const DistributedParamServer&) = delete;
    DistributedParamServer& operator=(const DistributedParamServer&) = delete;
    DistributedParamServer(DistributedParamServer&&) noexcept;
    DistributedParamServer& operator=(DistributedParamServer&&) noexcept;

    bool declare(const ParamDesc& desc) override;
    std::optional<ParamValue> get(const std::string& key) const override;
    bool set(const std::string& key, const ParamValue& value) override;
    void subscribe(const std::string& key, IParamObserver* observer) override;

    // 启用从 HTTP 端点周期拉取（返回 key=value 行）。
    // 提示：libcurl 支持 `file:///abs/path/to/file`，便于测试。
    void setHttpFetch(const std::string& url, std::chrono::milliseconds interval);
    void setHttpFetchList(const std::vector<std::string>& urls, std::chrono::milliseconds interval);

    // 配置周期拉取适配器：调用方提供一个函数用于拉取 key/value 更新。
    // 用于桥接到 Consul/etcd/HTTP watch 等外部系统。
    void setFetchCallback(FetchCallback cb, std::chrono::milliseconds interval);

    // 启用 param.export 调试 RPC 服务。
    // 请求 JSON：{"op":"param.export","id":"<optional>"}
    // 响应 JSON：{"op":"param.export","id":"<optional>","status":"ok","params":{...}}
    void enableExportService(std::string request_topic, std::string reply_topic);

    // 可选：持久化快照（委托给内部 communicator 版 ParamServer）。
    void setSnapshotPath(std::string path);
    void loadSnapshot();
    void saveSnapshot() const;

    // 内部工作线程至少进入过一次主循环后返回 true。
    // 当配置要求参数服务已“运行起来”时，可用于 readiness gating。
    bool hasEnteredLoop() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wxz::core
