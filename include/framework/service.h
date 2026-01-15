#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "executor.h"
#include "logger.h"
#include "rpc/rpc_service.h"
#include "service_common.h"
#include "strand.h"

#include "framework/status.h"

namespace wxz::framework {

/// 基于 MotionCore RpcServer 的薄封装（ROS2-like create_service）。
///
/// 目标：
/// - 固化 request/reply topic 约定（/svc/<name>/rpc/*）
/// - 固化“回调不跑在 DDS listener 线程上”的投递策略（bind_scheduler）
/// - 业务侧用 Status 表达成功/失败与原因
class RpcService {
public:
    using Json = wxz::core::rpc::RpcServer::Json;

    struct Options {
        int domain{0};
        std::string service;
        std::string sw_version;

        // 为空时由默认约定推导：/svc/<service>/rpc/request|reply
        std::string request_topic;
        std::string reply_topic;

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
        std::string sw_version;

        wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos();
        std::string metrics_scope;
    };

    struct Reply {
        Status status;
        Json result = Json::object();
    };

    using Handler = std::function<Reply(const Json& params)>;

    explicit RpcService(Options opts)
        : RpcService([&] {
            Config cfg;
            cfg.domain = opts.domain;
            cfg.service_name = std::move(opts.service);
            cfg.sw_version = std::move(opts.sw_version);
            cfg.request_topic = std::move(opts.request_topic);
            cfg.reply_topic = std::move(opts.reply_topic);
            cfg.qos = std::move(opts.qos);
            cfg.metrics_scope = std::move(opts.metrics_scope);
            return cfg;
        }()) {}

    explicit RpcService(Config cfg)
        : cfg_(std::move(cfg)) {
        wxz::core::rpc::RpcServerOptions opts;
        opts.domain = cfg_.domain;
        opts.request_topic = cfg_.request_topic;
        opts.reply_topic = cfg_.reply_topic;
        opts.service_name = cfg_.service_name;
        opts.qos = cfg_.qos;
        opts.metrics_scope = cfg_.metrics_scope;
        server_ = std::make_unique<wxz::core::rpc::RpcServer>(std::move(opts));
    }

    RpcService(const RpcService&) = delete;
    RpcService& operator=(const RpcService&) = delete;

    void bind_scheduler(wxz::core::Executor& ex) { server_->bind_scheduler(ex); }
    void bind_scheduler(wxz::core::Strand& strand) { server_->bind_scheduler(strand); }

    /// 注册一个 ping handler（便于统一探活/版本信息）。
    void add_ping_handler(std::string op = "ping") {
        server_->add_handler(std::move(op), [&](const Json&) {
            wxz::core::rpc::RpcServer::Reply rep;
            rep.result = Json{{"service", cfg_.service_name},
                              {"sw_version", cfg_.sw_version},
                              {"domain", cfg_.domain},
                              {"ts_ms", wxz::core::now_epoch_ms()}};
            return rep;
        });
    }

    /// 注册业务 handler。
    ///
    /// - 返回 Status::ok=true 表示成功；ok=false 时会映射到 RpcServer::Reply.ok=false。
    /// - 失败时 reason 优先使用 status.err；若为空则使用 "error"。
    void add_handler(std::string op, Handler handler) {
        server_->add_handler(std::move(op), [h = std::move(handler)](const Json& params) {
            const Reply r = h(params);
            wxz::core::rpc::RpcServer::Reply rep;
            rep.ok = r.status.ok;
            if (!r.status.ok) {
                rep.reason = !r.status.err.empty() ? r.status.err : "error";
            }
            rep.result = r.result;
            return rep;
        });
    }

    bool start(wxz::core::Logger* logger = nullptr) {
        if (!server_->start()) {
            if (logger) {
                logger->log(wxz::core::LogLevel::Warn, "RPC enabled but failed to start (ignored)");
            }
            return false;
        }

        if (logger) {
            logger->log(wxz::core::LogLevel::Info,
                        "RPC enabled request='" + cfg_.request_topic + "' reply='" + cfg_.reply_topic + "'");
        }
        return true;
    }

    void stop() { server_->stop(); }

private:
    Config cfg_;
    std::unique_ptr<wxz::core::rpc::RpcServer> server_;
};

struct RpcService::Options::Builder {
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

    Builder& sw_version(std::string v) {
        opts.sw_version = std::move(v);
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

inline RpcService::Options::Builder RpcService::Options::builder() {
    return Builder();
}

inline RpcService::Options::Builder RpcService::Options::builder(std::string service) {
    return Builder(std::move(service));
}

inline std::string default_rpc_request_topic(std::string_view service) {
    return "/svc/" + std::string(service) + "/rpc/request";
}

inline std::string default_rpc_reply_topic(std::string_view service) {
    return "/svc/" + std::string(service) + "/rpc/reply";
}

inline RpcService::Config default_rpc_service_config(int domain,
                                                    std::string_view service,
                                                    std::string_view sw_version,
                                                    std::string request_topic = {},
                                                    std::string reply_topic = {}) {
    RpcService::Config cfg;
    cfg.domain = domain;
    cfg.service_name = std::string(service);
    cfg.sw_version = std::string(sw_version);
    cfg.request_topic = request_topic.empty() ? default_rpc_request_topic(service) : std::move(request_topic);
    cfg.reply_topic = reply_topic.empty() ? default_rpc_reply_topic(service) : std::move(reply_topic);
    return cfg;
}

} // namespace wxz::framework
