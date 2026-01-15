#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "executor.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_common.h"
#include "strand.h"

#include "framework/service.h" // default_rpc_request_topic/default_rpc_reply_topic
#include "framework/status.h"

namespace wxz::framework {

/// 基于 MotionCore RpcClient 的薄封装（ROS2-like create_client）。
///
/// 目标：
/// - 固化 /svc/<service>/rpc/* topic 约定
/// - 将 RpcErrorCode 映射为统一 Status
/// - 仍保持底层能力：bind_scheduler + start/stop + timeout
class RpcServiceClient {
public:
    using Json = wxz::core::rpc::RpcClient::Json;

    struct Options {
        int domain{0};
        std::string service;

        // 用于生成请求 id 的前缀（便于跨进程排障）。
        std::string client_id_prefix;

        // 为空时由默认约定推导：/svc/<service>/rpc/request|reply
        std::string request_topic;
        std::string reply_topic;

        // 默认超时（供 call(op, params) 使用）。
        std::chrono::milliseconds timeout{1000};

        wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos();

        // 可观测性标签：建议填 service 名称。
        std::string metrics_scope;

        struct Builder;
        static Builder builder();
        static Builder builder(std::string service);
    };

    struct Config {
        int domain{0};
        std::string service_name;
        std::string request_topic;
        std::string reply_topic;
        std::string client_id_prefix;

        std::chrono::milliseconds default_timeout{1000};
        wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos();
        std::string metrics_scope;
    };

    struct Reply {
        Status status;
        Json result = Json::object();
    };

    explicit RpcServiceClient(Options opts)
        : RpcServiceClient([&] {
            Config cfg;
            cfg.domain = opts.domain;
            cfg.service_name = std::move(opts.service);
            cfg.client_id_prefix = std::move(opts.client_id_prefix);
            cfg.request_topic = std::move(opts.request_topic);
            cfg.reply_topic = std::move(opts.reply_topic);
            cfg.default_timeout = opts.timeout;
            cfg.qos = std::move(opts.qos);
            cfg.metrics_scope = std::move(opts.metrics_scope);
            return cfg;
        }()) {}

    explicit RpcServiceClient(Config cfg)
        : cfg_(std::move(cfg)) {
        wxz::core::rpc::RpcClientOptions opts;
        opts.domain = cfg_.domain;
        opts.request_topic = cfg_.request_topic;
        opts.reply_topic = cfg_.reply_topic;
        opts.client_id_prefix = cfg_.client_id_prefix;
        opts.qos = cfg_.qos;
        opts.metrics_scope = cfg_.metrics_scope;
        client_ = std::make_unique<wxz::core::rpc::RpcClient>(std::move(opts));
    }

    RpcServiceClient(const RpcServiceClient&) = delete;
    RpcServiceClient& operator=(const RpcServiceClient&) = delete;

    void bind_scheduler(wxz::core::Executor& ex) { client_->bind_scheduler(ex); }
    void bind_scheduler(wxz::core::Strand& strand) { client_->bind_scheduler(strand); }

    bool start() { return client_->start(); }
    void stop() { client_->stop(); }

    /// 同步调用。
    /// - status.ok=true 表示 RPC 成功且对端返回 ok。
    /// - status.ok=false 表示 timeout/transport/remote error 等。
    Reply call(const std::string& op, const Json& params, std::chrono::milliseconds timeout) {
        const auto r = client_->call(op, params, timeout);
        Reply rep;
        rep.result = r.result;

        if (r.ok()) {
            rep.status = Status::ok_status();
            return rep;
        }

        rep.status.ok = false;
        rep.status.err_code = static_cast<int>(r.code);
        rep.status.err = !r.reason.empty() ? r.reason : std::string(wxz::core::rpc::to_string(r.code));
        return rep;
    }

    Reply call(const std::string& op, const Json& params) {
        return call(op, params, cfg_.default_timeout);
    }

private:
    Config cfg_;
    std::unique_ptr<wxz::core::rpc::RpcClient> client_;
};

struct RpcServiceClient::Options::Builder {
    Options opts;

    Builder() = default;

    explicit Builder(std::string service) {
        opts.service = std::move(service);
    }

    Builder& service(std::string v) {
        opts.service = std::move(v);
        return *this;
    }

    Builder& domain(int v) {
        opts.domain = v;
        return *this;
    }

    Builder& client_id_prefix(std::string v) {
        opts.client_id_prefix = std::move(v);
        return *this;
    }

    Builder& request_topic(std::string v) {
        opts.request_topic = std::move(v);
        return *this;
    }

    Builder& reply_topic(std::string v) {
        opts.reply_topic = std::move(v);
        return *this;
    }

    Builder& timeout(std::chrono::milliseconds v) {
        opts.timeout = v;
        return *this;
    }

    Builder& qos(wxz::core::ChannelQoS v) {
        opts.qos = std::move(v);
        return *this;
    }

    Builder& metrics_scope(std::string v) {
        opts.metrics_scope = std::move(v);
        return *this;
    }

    Options build() && { return std::move(opts); }
    operator Options() && { return std::move(opts); }
};

inline RpcServiceClient::Options::Builder RpcServiceClient::Options::builder() {
    return Builder();
}

inline RpcServiceClient::Options::Builder RpcServiceClient::Options::builder(std::string service) {
    return Builder(std::move(service));
}

inline RpcServiceClient::Config default_rpc_client_config(int domain,
                                                         std::string_view service,
                                                         std::string client_id_prefix = {},
                                                         std::string request_topic = {},
                                                         std::string reply_topic = {}) {
    RpcServiceClient::Config cfg;
    cfg.domain = domain;
    cfg.service_name = std::string(service);
    cfg.client_id_prefix = std::move(client_id_prefix);
    cfg.request_topic = request_topic.empty() ? default_rpc_request_topic(service) : std::move(request_topic);
    cfg.reply_topic = reply_topic.empty() ? default_rpc_reply_topic(service) : std::move(reply_topic);
    return cfg;
}

} // namespace wxz::framework
