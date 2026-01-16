#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "byte_buffer_pool.h"
#include "dto/event_dto.h"
#include "dto/event_dto_cdr.h"
#include "executor.h"
#include "fastdds_channel.h"
#include "logger.h"
#include "node_base.h"
#include "observability.h"
#include "param_server.h"
#include "service_common.h"
#include "strand.h"

#include "framework/parameter.h"
#include "framework/qos.h"
#include "framework/service.h"
#include "framework/service_client.h"
#include "framework/timer.h"

namespace wxz::framework {

/// 订阅侧 drop/拒绝等统计。
struct SubscriptionStats {
    std::atomic<std::uint64_t> recv{0};

    // 由薄封装统计：
    std::atomic<std::uint64_t> drop_decode_failed{0};
    std::atomic<std::uint64_t> drop_schema_mismatch{0};
    std::atomic<std::uint64_t> drop_user_exception{0};
};

/// EventDTO（CDR）订阅：
/// - DDS listener 线程只做拷贝；业务回调由 strand/executor 驱动
/// - 自动 decode + schema 校验 + drop 统计
class EventDtoSubscription {
public:
    using Callback = std::function<void(const ::EventDTO& dto)>;

    struct Options {
        int domain{0};
        std::string topic;
        std::string expected_schema_id;

        wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos();

        // CDR payload 最大长度（用于 FastddsChannel 与 buffer pool）。
        std::size_t dto_max_payload{8 * 1024};

        // leased buffer pool 容量。
        std::size_t pool_buffers{64};

        // 可观测性标签：建议填 service 名称。
        std::string metrics_scope;

        struct Builder;
        static Builder builder();
        static Builder builder(std::string topic);
    };

    EventDtoSubscription(Options opts,
                        wxz::core::Strand& strand,
                        Callback cb,
                        wxz::core::Logger* logger = nullptr)
        : opts_(std::move(opts)),
          pool_(wxz::core::ByteBufferPool::Options{opts_.pool_buffers, opts_.dto_max_payload}),
          chan_(opts_.domain,
                opts_.topic,
                opts_.qos,
                opts_.dto_max_payload,
                /*enable_pub=*/false,
                /*enable_sub=*/true),
          cb_(std::move(cb)),
          logger_(logger) {
        subscribe_on(strand);
    }

        EventDtoSubscription(Options opts,
                                                wxz::core::Executor& ex,
                                                Callback cb,
                                                wxz::core::Logger* logger = nullptr)
                : opts_(std::move(opts)),
                    pool_(wxz::core::ByteBufferPool::Options{opts_.pool_buffers, opts_.dto_max_payload}),
                    chan_(opts_.domain,
                                opts_.topic,
                                opts_.qos,
                                opts_.dto_max_payload,
                                /*enable_pub=*/false,
                                /*enable_sub=*/true),
                    cb_(std::move(cb)),
                    logger_(logger) {
                subscribe_on(ex);
        }

    const SubscriptionStats& stats() const { return stats_; }
    const wxz::core::FastddsChannel& channel() const { return chan_; }

private:
    void emit_counter(std::string_view name, double v, std::string_view reason) {
        if (!wxz::core::has_metrics_sink()) return;
        wxz::core::metrics().counter_add(
            name,
            v,
            {
                {"scope", opts_.metrics_scope},
                {"topic", opts_.topic},
                {"reason", reason},
            });
    }

    template <class Scheduler>
    void subscribe_on(Scheduler& scheduler) {
        chan_.subscribe_leased_on(pool_, scheduler, [&](wxz::core::ByteBufferLease&& msg) {
            ::EventDTO dto;
            if (!wxz::dto::decode_event_dto_cdr(msg.data(), msg.size(), dto)) {
                stats_.drop_decode_failed.fetch_add(1);
                emit_counter("wxz.workstation.subscription.drop", 1, "decode_failed");
                if (logger_) logger_->log(wxz::core::LogLevel::Warn, "drop: decode_event_dto_cdr failed");
                return;
            }

            if (!opts_.expected_schema_id.empty() && dto.schema_id != opts_.expected_schema_id) {
                stats_.drop_schema_mismatch.fetch_add(1);
                emit_counter("wxz.workstation.subscription.drop", 1, "schema_mismatch");
                if (logger_) {
                    logger_->log(wxz::core::LogLevel::Warn,
                                 "drop: unexpected schema_id='" + dto.schema_id + "' expected='" + opts_.expected_schema_id + "'");
                }
                return;
            }

            stats_.recv.fetch_add(1);
            if (wxz::core::has_metrics_sink()) {
                wxz::core::metrics().counter_add(
                    "wxz.workstation.subscription.recv",
                    1,
                    {
                        {"scope", opts_.metrics_scope},
                        {"topic", opts_.topic},
                    });
            }

            try {
                cb_(dto);
            } catch (...) {
                stats_.drop_user_exception.fetch_add(1);
                emit_counter("wxz.workstation.subscription.drop", 1, "user_exception");
                if (logger_) logger_->log(wxz::core::LogLevel::Warn, "drop: user callback threw exception");
            }
        });
    }

    Options opts_;
    wxz::core::ByteBufferPool pool_;
    wxz::core::FastddsChannel chan_;
    Callback cb_;
    wxz::core::Logger* logger_{nullptr};
    SubscriptionStats stats_;
};

struct EventDtoSubscription::Options::Builder {
    Options opts;

    Builder() = default;

    explicit Builder(std::string topic) {
        opts.topic = std::move(topic);
    }

    Builder& topic(std::string v) {
        opts.topic = std::move(v);
        return *this;
    }

    Builder& domain(int v) {
        opts.domain = v;
        return *this;
    }

    Builder& schema_id(std::string v) {
        opts.expected_schema_id = std::move(v);
        return *this;
    }

    Builder& qos(wxz::core::ChannelQoS v) {
        opts.qos = std::move(v);
        return *this;
    }

    Builder& max_payload(std::size_t v) {
        opts.dto_max_payload = v;
        return *this;
    }

    Builder& pool_buffers(std::size_t v) {
        opts.pool_buffers = v;
        return *this;
    }

    Builder& metrics_scope(std::string v) {
        opts.metrics_scope = std::move(v);
        return *this;
    }

    Options build() && { return std::move(opts); }
    operator Options() && { return std::move(opts); }
};

inline EventDtoSubscription::Options::Builder EventDtoSubscription::Options::builder() {
    return Builder();
}

inline EventDtoSubscription::Options::Builder EventDtoSubscription::Options::builder(std::string topic) {
    return Builder(std::move(topic));
}

/// EventDTO（CDR）发布：封装 encode 与基础统计。
class EventDtoPublisher {
public:
    struct Options {
        int domain{0};
        std::string topic;
        wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos();
        std::size_t dto_max_payload{8 * 1024};

        // 可观测性标签：建议填 service 名称。
        std::string metrics_scope;

        struct Builder;
        static Builder builder();
        static Builder builder(std::string topic);
    };

    explicit EventDtoPublisher(Options opts)
        : opts_(std::move(opts)),
          chan_(opts_.domain,
                opts_.topic,
                opts_.qos,
                opts_.dto_max_payload,
                /*enable_pub=*/true,
                /*enable_sub=*/false) {}

    bool publish(const ::EventDTO& dto) {
        std::vector<std::uint8_t> buf;
        if (!wxz::dto::encode_event_dto_cdr(dto, buf, /*initial_reserve=*/opts_.dto_max_payload)) {
            if (wxz::core::has_metrics_sink()) {
                wxz::core::metrics().counter_add(
                    "wxz.workstation.publisher.drop",
                    1,
                    {{"scope", opts_.metrics_scope}, {"topic", opts_.topic}, {"reason", "encode_failed"}});
            }
            return false;
        }

        const bool ok = !buf.empty() && chan_.publish(buf.data(), buf.size());
        if (wxz::core::has_metrics_sink()) {
            wxz::core::metrics().counter_add(
                ok ? "wxz.workstation.publisher.ok" : "wxz.workstation.publisher.drop",
                1,
                {{"scope", opts_.metrics_scope}, {"topic", opts_.topic}, {"reason", ok ? "ok" : "publish_failed"}});
        }
        return ok;
    }

    const wxz::core::FastddsChannel& channel() const { return chan_; }

private:
    Options opts_;
    wxz::core::FastddsChannel chan_;
};

struct EventDtoPublisher::Options::Builder {
    Options opts;

    Builder() = default;

    explicit Builder(std::string topic) {
        opts.topic = std::move(topic);
    }

    Builder& topic(std::string v) {
        opts.topic = std::move(v);
        return *this;
    }

    Builder& domain(int v) {
        opts.domain = v;
        return *this;
    }

    Builder& qos(wxz::core::ChannelQoS v) {
        opts.qos = std::move(v);
        return *this;
    }

    Builder& max_payload(std::size_t v) {
        opts.dto_max_payload = v;
        return *this;
    }

    Builder& metrics_scope(std::string v) {
        opts.metrics_scope = std::move(v);
        return *this;
    }

    Options build() && { return std::move(opts); }
    operator Options() && { return std::move(opts); }
};

inline EventDtoPublisher::Options::Builder EventDtoPublisher::Options::builder() {
    return Builder();
}

inline EventDtoPublisher::Options::Builder EventDtoPublisher::Options::builder(std::string topic) {
    return Builder(std::move(topic));
}

/// 纯文本订阅（例如 payload 为 "k=v;..." 的 KV 文本）。
/// - DDS listener 线程不执行业务：通过 FastddsChannel::subscribe_on 投递到 strand。
/// - 适合低频控制面/诊断类 topic（例如 fault/action）。
class TextSubscription {
public:
    using Callback = std::function<void(std::string msg)>;

    struct Options {
        int domain{0};
        std::string topic;
        wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos();

        // 单条消息最大长度（用于 FastddsChannel 内部 buffer）。
        std::size_t max_payload{2048};

        // 可观测性标签：建议填 service 名称。
        std::string metrics_scope;

        struct Builder;
        static Builder builder();
        static Builder builder(std::string topic);
    };

    TextSubscription(Options opts,
                     wxz::core::Strand& strand,
                     Callback cb,
                     wxz::core::Logger* logger = nullptr)
        : opts_(std::move(opts)),
          chan_(opts_.domain,
                opts_.topic,
                opts_.qos,
                opts_.max_payload,
                /*enable_pub=*/false,
                /*enable_sub=*/true),
          cb_(std::move(cb)),
          logger_(logger) {
                subscribe_on(strand);
        }

        TextSubscription(Options opts,
                                         wxz::core::Executor& ex,
                                         Callback cb,
                                         wxz::core::Logger* logger = nullptr)
                : opts_(std::move(opts)),
                    chan_(opts_.domain,
                                opts_.topic,
                                opts_.qos,
                                opts_.max_payload,
                                /*enable_pub=*/false,
                                /*enable_sub=*/true),
                    cb_(std::move(cb)),
                    logger_(logger) {
                subscribe_on(ex);
        }

        const SubscriptionStats& stats() const { return stats_; }
        const wxz::core::FastddsChannel& channel() const { return chan_; }

private:
        template <class Scheduler>
        void subscribe_on(Scheduler& scheduler) {
                chan_.subscribe_on(scheduler, [&](const std::uint8_t* data, std::size_t size) {
            stats_.recv.fetch_add(1);
            if (wxz::core::has_metrics_sink()) {
                wxz::core::metrics().counter_add(
                    "wxz.workstation.subscription.recv",
                    1,
                    {{"scope", opts_.metrics_scope}, {"topic", opts_.topic}});
            }

            try {
                cb_(std::string(reinterpret_cast<const char*>(data), size));
            } catch (...) {
                stats_.drop_user_exception.fetch_add(1);
                if (wxz::core::has_metrics_sink()) {
                    wxz::core::metrics().counter_add(
                        "wxz.workstation.subscription.drop",
                        1,
                        {{"scope", opts_.metrics_scope}, {"topic", opts_.topic}, {"reason", "user_exception"}});
                }
                if (logger_) logger_->log(wxz::core::LogLevel::Warn, "drop: user callback threw exception");
            }
        });
    }
    Options opts_;
    wxz::core::FastddsChannel chan_;
    Callback cb_;
    wxz::core::Logger* logger_{nullptr};
    SubscriptionStats stats_;
};

/// Callback group 类型：
/// - MutuallyExclusive: 串行化回调（基于 Strand）
/// - Reentrant: 允许并发回调（基于 Executor）
enum class CallbackGroupType { MutuallyExclusive, Reentrant };

/// CallbackGroup：包装一个用于分派回调的 scheduler（要么是 Strand，要么是 Executor）。
class CallbackGroup {
public:
    explicit CallbackGroup(CallbackGroupType t, wxz::core::Executor& ex)
        : type_(t), executor_(&ex) {
        if (type_ == CallbackGroupType::MutuallyExclusive) {
            owned_strand_ = std::make_unique<wxz::core::Strand>(*executor_);
            strand_ = owned_strand_.get();
        }
    }

    // MutuallyExclusive 复用外部 strand（不拥有）。
    CallbackGroup(wxz::core::Executor& ex, wxz::core::Strand& strand)
        : type_(CallbackGroupType::MutuallyExclusive), executor_(&ex), strand_(&strand) {}

    CallbackGroupType type() const { return type_; }
    wxz::core::Executor* executor() const { return executor_; }
    wxz::core::Strand* strand() const { return strand_; }

private:
    CallbackGroupType type_;
    wxz::core::Executor* executor_{nullptr};
    std::unique_ptr<wxz::core::Strand> owned_strand_;
    wxz::core::Strand* strand_{nullptr};
};

struct TextSubscription::Options::Builder {
    Options opts;

    Builder() = default;

    explicit Builder(std::string topic) {
        opts.topic = std::move(topic);
    }

    Builder& topic(std::string v) {
        opts.topic = std::move(v);
        return *this;
    }

    Builder& domain(int v) {
        opts.domain = v;
        return *this;
    }

    Builder& qos(wxz::core::ChannelQoS v) {
        opts.qos = std::move(v);
        return *this;
    }

    Builder& max_payload(std::size_t v) {
        opts.max_payload = v;
        return *this;
    }

    Builder& metrics_scope(std::string v) {
        opts.metrics_scope = std::move(v);
        return *this;
    }

    Options build() && { return std::move(opts); }
    operator Options() && { return std::move(opts); }
};

inline TextSubscription::Options::Builder TextSubscription::Options::builder() {
    return Builder();
}

inline TextSubscription::Options::Builder TextSubscription::Options::builder(std::string topic) {
    return Builder(std::move(topic));
}

/// ROS2-like Node：将 NodeBase + Executor/Strand 的常用模式固化。
class Node {
public:
    using SharedPtr = std::shared_ptr<Node>;
    using WeakPtr = std::weak_ptr<Node>;

    using EventDtoSubscriptionPtr = std::shared_ptr<EventDtoSubscription>;
    using EventDtoPublisherPtr = std::shared_ptr<EventDtoPublisher>;
    using TextSubscriptionPtr = std::shared_ptr<TextSubscription>;
    using RpcServicePtr = std::shared_ptr<RpcService>;
    using RpcServiceClientPtr = std::shared_ptr<RpcServiceClient>;

    struct Options {
        wxz::core::NodeBaseConfig base;
        wxz::core::Executor* executor{nullptr};

        // 默认绑定到该 strand；用于“回调不跑 DDS listener 线程”。
        wxz::core::Strand* default_strand{nullptr};

        // 可选：用于薄封装内部日志。
        wxz::core::Logger* logger{nullptr};

        // 可观测性标签。
        std::string metrics_scope;

        // 参数服务：
        // - 为空 => Node 内部默认创建进程内 ParamServer。
        // - 非空 => 注入已有 IParamServer（例如分布式参数服务）。
        std::shared_ptr<wxz::core::IParamServer> param_server;
    };

    using CallbackGroupPtr = std::shared_ptr<CallbackGroup>;

    explicit Node(Options opts)
        : base_(std::move(opts.base)),
          executor_(opts.executor),
          default_strand_(opts.default_strand),
          logger_(opts.logger),
          metrics_scope_(std::move(opts.metrics_scope)),
          params_(std::move(opts.param_server)) {
        // ROS2-like 默认行为：若未注入 executor/default_strand，则 Node 自己创建。
        // - 默认 executor.threads=0：由主循环 spin_once() 驱动（更接近 rclcpp::spin 的模型）。
        if (!executor_) {
            wxz::core::Executor::Options ex_opts;
            ex_opts.threads = 0;
            owned_executor_ = std::make_unique<wxz::core::Executor>(ex_opts);
            (void)owned_executor_->start();
            executor_ = owned_executor_.get();
        }

        if (!default_strand_) {
            owned_default_strand_ = std::make_unique<wxz::core::Strand>(*executor_);
            default_strand_ = owned_default_strand_.get();
        }

        // ROS2-like：默认 callback group = MutuallyExclusive，并复用 default_strand。
        default_callback_group_ = std::make_shared<CallbackGroup>(*executor_, *default_strand_);

        // 定时器默认绑定到 default_strand（保持“回调串行化 + 不占用 DDS listener 线程”）。
        timers_.bind_scheduler(*default_strand_);
    }

    /// 创建 CallbackGroup：
    /// - `MutuallyExclusive` 返回基于 Strand 的串行组
    /// - `Reentrant` 返回基于 Executor 的并发组
    CallbackGroupPtr create_callback_group(CallbackGroupType type) {
        return std::make_shared<CallbackGroup>(type, *executor_);
    }

    wxz::core::NodeBase& base() { return base_; }
    const wxz::core::NodeBase& base() const { return base_; }

    /// 便捷转发：与 rclcpp::Node 风格一致。
    bool running() const { return base_.running(); }
    void install_signal_handlers() { base_.install_signal_handlers(); }

    wxz::core::Executor& executor() { return *executor_; }
    wxz::core::Strand& default_strand() { return *default_strand_; }

    CallbackGroup& default_callback_group() { return *default_callback_group_; }
    const CallbackGroup& default_callback_group() const { return *default_callback_group_; }
    CallbackGroupPtr default_callback_group_ptr() const { return default_callback_group_; }

    /// 在主循环里调用：统一 tick（NodeBase + timers）。
    void tick() {
        base_.tick();
        (void)tick_timers();
    }

    /// 访问参数集合。
    Parameters& parameters() { return params_; }
    const Parameters& parameters() const { return params_; }

    /// ROS2-like 参数便捷接口：直接挂在 Node 上。
    bool declare_parameter(std::string name,
                           Parameters::Value default_value,
                           std::string schema = {},
                           bool read_only = false) {
        return params_.declare(std::move(name), std::move(default_value), std::move(schema), read_only);
    }

    std::optional<Parameters::Value> get_parameter(const std::string& key) const {
        return params_.get(key);
    }

    template <class T>
    T get_parameter_or(const std::string& key, T def) const {
        const auto v = params_.get(key);
        if (!v.has_value()) return def;
        if (auto p = std::get_if<T>(&*v)) return *p;
        return def;
    }

    Status set_parameter(const std::string& key, const Parameters::Value& value) {
        return params_.set(key, value);
    }

    /// 在主循环里调用：触发到期 timer。
    /// - 建议与 base().tick()、executor().spin_once() 一起使用。
    bool tick_timers() { return timers_.tick(); }

    /// 创建 wall timer（回调投递到默认 scheduler）。
    TimerManager::TimerHandle create_wall_timer(std::chrono::milliseconds period,
                                               TimerManager::Callback cb) {
        return timers_.create_wall_timer(period, std::move(cb));
    }

    /// RAII wall timer：析构时自动 cancel。
    class WallTimer {
    public:
        WallTimer() = default;
        WallTimer(const WallTimer&) = delete;
        WallTimer& operator=(const WallTimer&) = delete;

        WallTimer(WallTimer&& other) noexcept { *this = std::move(other); }
        WallTimer& operator=(WallTimer&& other) noexcept {
            if (this == &other) return *this;
            cancel();
            mgr_ = other.mgr_;
            handle_ = other.handle_;
            other.mgr_ = nullptr;
            other.handle_ = {};
            return *this;
        }

        ~WallTimer() { cancel(); }

        explicit operator bool() const { return mgr_ != nullptr && handle_.id != 0; }

        void cancel() {
            if (!mgr_ || handle_.id == 0) return;
            mgr_->cancel(handle_);
            mgr_ = nullptr;
            handle_ = {};
        }

    private:
        friend class Node;
        WallTimer(TimerManager& mgr, TimerManager::TimerHandle h) : mgr_(&mgr), handle_(h) {}
        TimerManager* mgr_{nullptr};
        TimerManager::TimerHandle handle_{};
    };

    WallTimer create_wall_timer_scoped(std::chrono::milliseconds period, TimerManager::Callback cb) {
        auto h = timers_.create_wall_timer(period, std::move(cb));
        return WallTimer(timers_, h);
    }

    std::unique_ptr<RpcService> create_service(RpcService::Options opts) {
        return create_service_on(default_callback_group(), std::move(opts));
    }

    std::unique_ptr<RpcService> create_service(RpcService::Options opts, CallbackGroup& group) {
        return create_service_on(group, std::move(opts));
    }

    std::unique_ptr<RpcService> create_service(RpcService::Options opts, CallbackGroupPtr group) {
        return create_service_on(resolve_group(group), std::move(opts));
    }

    std::unique_ptr<RpcService> create_service(CallbackGroup& group, RpcService::Options opts) {
        return create_service_on(group, std::move(opts));
    }

    std::unique_ptr<RpcService> create_service(CallbackGroupPtr group, RpcService::Options opts) {
        return create_service_on(resolve_group(group), std::move(opts));
    }

    std::unique_ptr<RpcService> create_service_on(wxz::core::Executor& ex, RpcService::Options opts) {
        opts.domain = base_.domain();
        if (opts.request_topic.empty()) opts.request_topic = default_rpc_request_topic(opts.service);
        if (opts.reply_topic.empty()) opts.reply_topic = default_rpc_reply_topic(opts.service);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        auto svc = std::make_unique<RpcService>(std::move(opts));
        svc->bind_scheduler(ex);
        return svc;
    }

    std::unique_ptr<RpcService> create_service_on(wxz::core::Strand& strand, RpcService::Options opts) {
        opts.domain = base_.domain();
        if (opts.request_topic.empty()) opts.request_topic = default_rpc_request_topic(opts.service);
        if (opts.reply_topic.empty()) opts.reply_topic = default_rpc_reply_topic(opts.service);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        auto svc = std::make_unique<RpcService>(std::move(opts));
        svc->bind_scheduler(strand);
        return svc;
    }

    std::unique_ptr<RpcService> create_service_on(CallbackGroup& group, RpcService::Options opts) {
        opts.domain = base_.domain();
        if (opts.request_topic.empty()) opts.request_topic = default_rpc_request_topic(opts.service);
        if (opts.reply_topic.empty()) opts.reply_topic = default_rpc_reply_topic(opts.service);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        auto svc = std::make_unique<RpcService>(std::move(opts));
        if (group.strand()) svc->bind_scheduler(*group.strand());
        else svc->bind_scheduler(*group.executor());
        return svc;
    }

    std::unique_ptr<RpcService> create_service_on(CallbackGroupPtr group, RpcService::Options opts) {
        return create_service_on(resolve_group(group), std::move(opts));
    }

    /// create_service：封装 RpcServer。
    /// - 默认 topic: /svc/<service>/rpc/request|reply
    /// - 默认将 handler 投递到 default_strand（否则退化到 executor）。
    std::unique_ptr<RpcService> create_service(std::string service,
                                               std::string sw_version,
                                               std::string request_topic = {},
                                               std::string reply_topic = {}) {
        auto cfg = default_rpc_service_config(base_.domain(), service, sw_version, std::move(request_topic), std::move(reply_topic));
        if (cfg.metrics_scope.empty()) cfg.metrics_scope = metrics_scope_;
        auto svc = std::make_unique<RpcService>(std::move(cfg));
        if (default_strand_) svc->bind_scheduler(*default_strand_);
        else if (executor_) svc->bind_scheduler(*executor_);
        return svc;
    }

    /// ROS2-like：CallbackGroup 作为后置参数。
    std::unique_ptr<RpcService> create_service(std::string service,
                                               std::string sw_version,
                                               CallbackGroup& group,
                                               std::string request_topic = {},
                                               std::string reply_topic = {}) {
        return create_service_on(group, std::move(service), std::move(sw_version), std::move(request_topic), std::move(reply_topic));
    }

    std::unique_ptr<RpcService> create_service(std::string service,
                                               std::string sw_version,
                                               CallbackGroupPtr group,
                                               std::string request_topic = {},
                                               std::string reply_topic = {}) {
        return create_service(std::move(service), std::move(sw_version), resolve_group(group), std::move(request_topic), std::move(reply_topic));
    }

    /// ROS2-like：直接指定 CallbackGroup（避免调用方显式 *_on）。
    std::unique_ptr<RpcService> create_service(CallbackGroup& group,
                                               std::string service,
                                               std::string sw_version,
                                               std::string request_topic = {},
                                               std::string reply_topic = {}) {
        return create_service_on(group, std::move(service), std::move(sw_version), std::move(request_topic), std::move(reply_topic));
    }

    std::unique_ptr<RpcService> create_service(CallbackGroupPtr group,
                                               std::string service,
                                               std::string sw_version,
                                               std::string request_topic = {},
                                               std::string reply_topic = {}) {
        return create_service(resolve_group(group), std::move(service), std::move(sw_version), std::move(request_topic), std::move(reply_topic));
    }

    std::unique_ptr<RpcService> create_service_on(wxz::core::Executor& ex,
                                                  std::string service,
                                                  std::string sw_version,
                                                  std::string request_topic = {},
                                                  std::string reply_topic = {}) {
        auto svc = create_service(std::move(service), std::move(sw_version), std::move(request_topic), std::move(reply_topic));
        if (svc) svc->bind_scheduler(ex);
        return svc;
    }

    std::unique_ptr<RpcService> create_service_on(wxz::core::Strand& strand,
                                                  std::string service,
                                                  std::string sw_version,
                                                  std::string request_topic = {},
                                                  std::string reply_topic = {}) {
        auto svc = create_service(std::move(service), std::move(sw_version), std::move(request_topic), std::move(reply_topic));
        if (svc) svc->bind_scheduler(strand);
        return svc;
    }

    std::unique_ptr<RpcService> create_service_on(CallbackGroup& group,
                                                  std::string service,
                                                  std::string sw_version,
                                                  std::string request_topic = {},
                                                  std::string reply_topic = {}) {
        auto svc = create_service(std::move(service), std::move(sw_version), std::move(request_topic), std::move(reply_topic));
        if (!svc) return svc;
        if (group.strand()) svc->bind_scheduler(*group.strand());
        else svc->bind_scheduler(*group.executor());
        return svc;
    }

    std::unique_ptr<RpcService> create_service_on(CallbackGroupPtr group,
                                                  std::string service,
                                                  std::string sw_version,
                                                  std::string request_topic = {},
                                                  std::string reply_topic = {}) {
        return create_service_on(resolve_group(group),
                                 std::move(service),
                                 std::move(sw_version),
                                 std::move(request_topic),
                                 std::move(reply_topic));
    }

    /// create_client：封装 RpcClient。
    /// - 默认 topic: /svc/<service>/rpc/request|reply
    /// - 默认将内部回调投递到 default_strand（否则退化到 executor）。
    std::unique_ptr<RpcServiceClient> create_client(std::string service,
                                                    std::string client_id_prefix = {},
                                                    std::string request_topic = {},
                                                    std::string reply_topic = {}) {
        auto cfg = default_rpc_client_config(base_.domain(), service, std::move(client_id_prefix), std::move(request_topic), std::move(reply_topic));
        if (cfg.metrics_scope.empty()) cfg.metrics_scope = metrics_scope_;
        auto cli = std::make_unique<RpcServiceClient>(std::move(cfg));
        if (default_strand_) cli->bind_scheduler(*default_strand_);
        else if (executor_) cli->bind_scheduler(*executor_);
        return cli;
    }

    std::unique_ptr<RpcServiceClient> create_client(RpcServiceClient::Options opts) {
        return create_client_on(default_callback_group(), std::move(opts));
    }

    std::unique_ptr<RpcServiceClient> create_client(RpcServiceClient::Options opts, CallbackGroup& group) {
        return create_client_on(group, std::move(opts));
    }

    std::unique_ptr<RpcServiceClient> create_client(RpcServiceClient::Options opts, CallbackGroupPtr group) {
        return create_client_on(resolve_group(group), std::move(opts));
    }

    std::unique_ptr<RpcServiceClient> create_client(CallbackGroup& group, RpcServiceClient::Options opts) {
        return create_client_on(group, std::move(opts));
    }

    std::unique_ptr<RpcServiceClient> create_client(CallbackGroupPtr group, RpcServiceClient::Options opts) {
        return create_client_on(resolve_group(group), std::move(opts));
    }

    std::unique_ptr<RpcServiceClient> create_client_on(wxz::core::Executor& ex, RpcServiceClient::Options opts) {
        opts.domain = base_.domain();
        if (opts.request_topic.empty()) opts.request_topic = default_rpc_request_topic(opts.service);
        if (opts.reply_topic.empty()) opts.reply_topic = default_rpc_reply_topic(opts.service);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        auto cli = std::make_unique<RpcServiceClient>(std::move(opts));
        cli->bind_scheduler(ex);
        return cli;
    }

    std::unique_ptr<RpcServiceClient> create_client_on(wxz::core::Strand& strand, RpcServiceClient::Options opts) {
        opts.domain = base_.domain();
        if (opts.request_topic.empty()) opts.request_topic = default_rpc_request_topic(opts.service);
        if (opts.reply_topic.empty()) opts.reply_topic = default_rpc_reply_topic(opts.service);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        auto cli = std::make_unique<RpcServiceClient>(std::move(opts));
        cli->bind_scheduler(strand);
        return cli;
    }

    std::unique_ptr<RpcServiceClient> create_client_on(CallbackGroup& group, RpcServiceClient::Options opts) {
        opts.domain = base_.domain();
        if (opts.request_topic.empty()) opts.request_topic = default_rpc_request_topic(opts.service);
        if (opts.reply_topic.empty()) opts.reply_topic = default_rpc_reply_topic(opts.service);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        auto cli = std::make_unique<RpcServiceClient>(std::move(opts));
        if (group.strand()) cli->bind_scheduler(*group.strand());
        else cli->bind_scheduler(*group.executor());
        return cli;
    }

    std::unique_ptr<RpcServiceClient> create_client_on(CallbackGroupPtr group, RpcServiceClient::Options opts) {
        return create_client_on(resolve_group(group), std::move(opts));
    }

    std::unique_ptr<RpcServiceClient> create_client(std::string service,
                                                    CallbackGroup& group,
                                                    std::string client_id_prefix = {},
                                                    std::string request_topic = {},
                                                    std::string reply_topic = {}) {
        return create_client_on(group,
                                std::move(service),
                                std::move(client_id_prefix),
                                std::move(request_topic),
                                std::move(reply_topic));
    }

    std::unique_ptr<RpcServiceClient> create_client(std::string service,
                                                    CallbackGroupPtr group,
                                                    std::string client_id_prefix = {},
                                                    std::string request_topic = {},
                                                    std::string reply_topic = {}) {
        return create_client(std::move(service), resolve_group(group), std::move(client_id_prefix), std::move(request_topic), std::move(reply_topic));
    }

    std::unique_ptr<RpcServiceClient> create_client(CallbackGroup& group,
                                                    std::string service,
                                                    std::string client_id_prefix = {},
                                                    std::string request_topic = {},
                                                    std::string reply_topic = {}) {
        return create_client_on(group,
                                std::move(service),
                                std::move(client_id_prefix),
                                std::move(request_topic),
                                std::move(reply_topic));
    }

    std::unique_ptr<RpcServiceClient> create_client(CallbackGroupPtr group,
                                                    std::string service,
                                                    std::string client_id_prefix = {},
                                                    std::string request_topic = {},
                                                    std::string reply_topic = {}) {
        return create_client(resolve_group(group),
                             std::move(service),
                             std::move(client_id_prefix),
                             std::move(request_topic),
                             std::move(reply_topic));
    }

    std::unique_ptr<RpcServiceClient> create_client_on(wxz::core::Executor& ex,
                                                       std::string service,
                                                       std::string client_id_prefix = {},
                                                       std::string request_topic = {},
                                                       std::string reply_topic = {}) {
        auto cli = create_client(std::move(service), std::move(client_id_prefix), std::move(request_topic), std::move(reply_topic));
        if (cli) cli->bind_scheduler(ex);
        return cli;
    }

    std::unique_ptr<RpcServiceClient> create_client_on(wxz::core::Strand& strand,
                                                       std::string service,
                                                       std::string client_id_prefix = {},
                                                       std::string request_topic = {},
                                                       std::string reply_topic = {}) {
        auto cli = create_client(std::move(service), std::move(client_id_prefix), std::move(request_topic), std::move(reply_topic));
        if (cli) cli->bind_scheduler(strand);
        return cli;
    }

    std::unique_ptr<RpcServiceClient> create_client_on(CallbackGroup& group,
                                                       std::string service,
                                                       std::string client_id_prefix = {},
                                                       std::string request_topic = {},
                                                       std::string reply_topic = {}) {
        auto cli = create_client(std::move(service), std::move(client_id_prefix), std::move(request_topic), std::move(reply_topic));
        if (!cli) return cli;
        if (group.strand()) cli->bind_scheduler(*group.strand());
        else cli->bind_scheduler(*group.executor());
        return cli;
    }

    std::unique_ptr<RpcServiceClient> create_client_on(CallbackGroupPtr group,
                                                       std::string service,
                                                       std::string client_id_prefix = {},
                                                       std::string request_topic = {},
                                                       std::string reply_topic = {}) {
        return create_client_on(resolve_group(group),
                                std::move(service),
                                std::move(client_id_prefix),
                                std::move(request_topic),
                                std::move(reply_topic));
    }

    /// create_subscription<EventDTO>：自动 leased + decode + schema 校验。
    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(std::string topic,
                                                                       std::string expected_schema_id,
                                                                       EventDtoSubscription::Callback cb,
                                                                       EventDtoSubscription::Options extra = {}) {
        EventDtoSubscription::Options opts = std::move(extra);
        opts.domain = base_.domain();
        opts.topic = std::move(topic);
        if (!expected_schema_id.empty()) opts.expected_schema_id = std::move(expected_schema_id);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        return std::make_unique<EventDtoSubscription>(std::move(opts), *default_strand_, std::move(cb), logger_);
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(std::string topic,
                                                                       std::string expected_schema_id,
                                                                       EventDtoSubscription::Callback cb,
                                                                       CallbackGroup& group,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(group,
                                               std::move(topic),
                                               std::move(expected_schema_id),
                                               std::move(cb),
                                               std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(std::string topic,
                                                                       std::string expected_schema_id,
                                                                       EventDtoSubscription::Callback cb,
                                                                       CallbackGroupPtr group,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto(std::move(topic),
                                            std::move(expected_schema_id),
                                            std::move(cb),
                                            resolve_group(group),
                                            std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(CallbackGroup& group,
                                                                       std::string topic,
                                                                       std::string expected_schema_id,
                                                                       EventDtoSubscription::Callback cb,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(group,
                                               std::move(topic),
                                               std::move(expected_schema_id),
                                               std::move(cb),
                                               std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(CallbackGroupPtr group,
                                                                       std::string topic,
                                                                       std::string expected_schema_id,
                                                                       EventDtoSubscription::Callback cb,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto(resolve_group(group),
                                            std::move(topic),
                                            std::move(expected_schema_id),
                                            std::move(cb),
                                            std::move(extra));
    }

    /// 更短签名：schema 可选（为空则不校验）。
    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(std::string topic,
                                                                       EventDtoSubscription::Callback cb,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto(std::move(topic), /*expected_schema_id=*/{}, std::move(cb), std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(std::string topic,
                                                                       EventDtoSubscription::Callback cb,
                                                                       CallbackGroup& group,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(group, std::move(topic), std::move(cb), std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(std::string topic,
                                                                       EventDtoSubscription::Callback cb,
                                                                       CallbackGroupPtr group,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto(std::move(topic), std::move(cb), resolve_group(group), std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(CallbackGroup& group,
                                                                       std::string topic,
                                                                       EventDtoSubscription::Callback cb,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(group, std::move(topic), std::move(cb), std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(CallbackGroupPtr group,
                                                                       std::string topic,
                                                                       EventDtoSubscription::Callback cb,
                                                                       EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto(resolve_group(group), std::move(topic), std::move(cb), std::move(extra));
    }

    /// 常用参数直传：避免手工构造 Options。
    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto(std::string topic,
                                                                       std::string expected_schema_id,
                                                                       EventDtoSubscription::Callback cb,
                                                                       wxz::core::ChannelQoS qos,
                                                                       std::size_t dto_max_payload = 8 * 1024,
                                                                       std::size_t pool_buffers = 64) {
        EventDtoSubscription::Options extra;
        extra.qos = qos;
        extra.dto_max_payload = dto_max_payload;
        extra.pool_buffers = pool_buffers;
        return create_subscription_eventdto(std::move(topic), std::move(expected_schema_id), std::move(cb), std::move(extra));
    }

    /// shared_ptr 句柄：更像 rclcpp 的 SharedPtr 使用方式。
    EventDtoSubscriptionPtr create_subscription_eventdto_shared(std::string topic,
                                                                std::string expected_schema_id,
                                                                EventDtoSubscription::Callback cb,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto(std::move(topic), std::move(expected_schema_id), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_shared(std::string topic,
                                                                std::string expected_schema_id,
                                                                EventDtoSubscription::Callback cb,
                                                                CallbackGroup& group,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto(std::move(topic), std::move(expected_schema_id), std::move(cb), group, std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_shared(std::string topic,
                                                                std::string expected_schema_id,
                                                                EventDtoSubscription::Callback cb,
                                                                CallbackGroupPtr group,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_shared(std::move(topic),
                                                      std::move(expected_schema_id),
                                                      std::move(cb),
                                                      resolve_group(group),
                                                      std::move(extra));
        return up;
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_shared(CallbackGroup& group,
                                                                std::string topic,
                                                                std::string expected_schema_id,
                                                                EventDtoSubscription::Callback cb,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto(group,
                                               std::move(topic),
                                               std::move(expected_schema_id),
                                               std::move(cb),
                                               std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_shared(std::string topic,
                                                                EventDtoSubscription::Callback cb,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto(std::move(topic), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_shared(std::string topic,
                                                                EventDtoSubscription::Callback cb,
                                                                CallbackGroup& group,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto(std::move(topic), std::move(cb), group, std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_shared(std::string topic,
                                                                EventDtoSubscription::Callback cb,
                                                                CallbackGroupPtr group,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_shared(std::move(topic), std::move(cb), resolve_group(group), std::move(extra));
        return up;
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_shared(CallbackGroup& group,
                                                                std::string topic,
                                                                EventDtoSubscription::Callback cb,
                                                                EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto(group, std::move(topic), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    /// 显式绑定到指定 strand（多串行域场景）。
    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(wxz::core::Strand& strand,
                                                                          std::string topic,
                                                                          std::string expected_schema_id,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        EventDtoSubscription::Options opts = std::move(extra);
        opts.domain = base_.domain();
        opts.topic = std::move(topic);
        if (!expected_schema_id.empty()) opts.expected_schema_id = std::move(expected_schema_id);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        return std::make_unique<EventDtoSubscription>(std::move(opts), strand, std::move(cb), logger_);
    }

    /// 显式绑定到指定 executor（允许并发回调；适合高吞吐/CPU 密集回调）。
    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(wxz::core::Executor& ex,
                                                                          std::string topic,
                                                                          std::string expected_schema_id,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        EventDtoSubscription::Options opts = std::move(extra);
        opts.domain = base_.domain();
        opts.topic = std::move(topic);
        if (!expected_schema_id.empty()) opts.expected_schema_id = std::move(expected_schema_id);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        return std::make_unique<EventDtoSubscription>(std::move(opts), ex, std::move(cb), logger_);
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(wxz::core::Strand& strand,
                                                                          std::string topic,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(strand, std::move(topic), /*expected_schema_id=*/{}, std::move(cb), std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(wxz::core::Executor& ex,
                                                                          std::string topic,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(ex, std::move(topic), /*expected_schema_id=*/{}, std::move(cb), std::move(extra));
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_on_shared(wxz::core::Strand& strand,
                                                                   std::string topic,
                                                                   std::string expected_schema_id,
                                                                   EventDtoSubscription::Callback cb,
                                                                   EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_on(strand, std::move(topic), std::move(expected_schema_id), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_on_shared(wxz::core::Executor& ex,
                                                                   std::string topic,
                                                                   std::string expected_schema_id,
                                                                   EventDtoSubscription::Callback cb,
                                                                   EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_on(ex, std::move(topic), std::move(expected_schema_id), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_on_shared(wxz::core::Executor& ex,
                                                                   std::string topic,
                                                                   EventDtoSubscription::Callback cb,
                                                                   EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_on(ex, std::move(topic), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    // 支持直接以 CallbackGroup 创建订阅（更接近 rclcpp 回调组体验）。
    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(CallbackGroup& group,
                                                                          std::string topic,
                                                                          std::string expected_schema_id,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        if (group.strand()) return create_subscription_eventdto_on(*group.strand(), std::move(topic), std::move(expected_schema_id), std::move(cb), std::move(extra));
        return create_subscription_eventdto_on(*group.executor(), std::move(topic), std::move(expected_schema_id), std::move(cb), std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(CallbackGroupPtr group,
                                                                          std::string topic,
                                                                          std::string expected_schema_id,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(resolve_group(group),
                                               std::move(topic),
                                               std::move(expected_schema_id),
                                               std::move(cb),
                                               std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(CallbackGroup& group,
                                                                          std::string topic,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        if (group.strand()) return create_subscription_eventdto_on(*group.strand(), std::move(topic), std::move(cb), std::move(extra));
        return create_subscription_eventdto_on(*group.executor(), std::move(topic), std::move(cb), std::move(extra));
    }

    std::unique_ptr<EventDtoSubscription> create_subscription_eventdto_on(CallbackGroupPtr group,
                                                                          std::string topic,
                                                                          EventDtoSubscription::Callback cb,
                                                                          EventDtoSubscription::Options extra = {}) {
        return create_subscription_eventdto_on(resolve_group(group),
                                               std::move(topic),
                                               std::move(cb),
                                               std::move(extra));
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_on_shared(CallbackGroup& group,
                                                                   std::string topic,
                                                                   std::string expected_schema_id,
                                                                   EventDtoSubscription::Callback cb,
                                                                   EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_on(group, std::move(topic), std::move(expected_schema_id), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_on_shared(CallbackGroupPtr group,
                                                                   std::string topic,
                                                                   std::string expected_schema_id,
                                                                   EventDtoSubscription::Callback cb,
                                                                   EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_on(resolve_group(group),
                                                  std::move(topic),
                                                  std::move(expected_schema_id),
                                                  std::move(cb),
                                                  std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_on_shared(CallbackGroup& group,
                                                                   std::string topic,
                                                                   EventDtoSubscription::Callback cb,
                                                                   EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_on(group, std::move(topic), std::move(cb), std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    EventDtoSubscriptionPtr create_subscription_eventdto_on_shared(CallbackGroupPtr group,
                                                                   std::string topic,
                                                                   EventDtoSubscription::Callback cb,
                                                                   EventDtoSubscription::Options extra = {}) {
        auto up = create_subscription_eventdto_on(resolve_group(group),
                                                  std::move(topic),
                                                  std::move(cb),
                                                  std::move(extra));
        return EventDtoSubscriptionPtr(up.release());
    }

    /// create_publisher<EventDTO>：自动 encode。
    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(std::string topic,
                                                                 std::size_t dto_max_payload = 8 * 1024,
                                                                 wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        EventDtoPublisher::Options opts;
        opts.domain = base_.domain();
        opts.topic = std::move(topic);
        opts.dto_max_payload = dto_max_payload;
        opts.qos = qos;
        opts.metrics_scope = metrics_scope_;
        return std::make_unique<EventDtoPublisher>(std::move(opts));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(std::string topic,
                                                                 CallbackGroup& group,
                                                                 std::size_t dto_max_payload = 8 * 1024,
                                                                 wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto_on(group, std::move(topic), dto_max_payload, std::move(qos));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(std::string topic,
                                                                 CallbackGroupPtr group,
                                                                 std::size_t dto_max_payload = 8 * 1024,
                                                                 wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto(std::move(topic), resolve_group(group), dto_max_payload, std::move(qos));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(CallbackGroup& group,
                                                                 std::string topic,
                                                                 std::size_t dto_max_payload = 8 * 1024,
                                                                 wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto_on(group, std::move(topic), dto_max_payload, std::move(qos));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(CallbackGroupPtr group,
                                                                 std::string topic,
                                                                 std::size_t dto_max_payload = 8 * 1024,
                                                                 wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto(resolve_group(group), std::move(topic), dto_max_payload, std::move(qos));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto_on(CallbackGroup&, std::string topic,
                                                                    std::size_t dto_max_payload = 8 * 1024,
                                                                    wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto(std::move(topic), dto_max_payload, std::move(qos));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto_on(CallbackGroupPtr,
                                                                    std::string topic,
                                                                    std::size_t dto_max_payload = 8 * 1024,
                                                                    wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto(std::move(topic), dto_max_payload, std::move(qos));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(EventDtoPublisher::Options extra) {
        EventDtoPublisher::Options opts = std::move(extra);
        opts.domain = base_.domain();
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        return std::make_unique<EventDtoPublisher>(std::move(opts));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(EventDtoPublisher::Options extra, CallbackGroup& group) {
        return create_publisher_eventdto_on(group, std::move(extra));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(EventDtoPublisher::Options extra, CallbackGroupPtr group) {
        return create_publisher_eventdto(std::move(extra), resolve_group(group));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(CallbackGroup& group, EventDtoPublisher::Options extra) {
        return create_publisher_eventdto_on(group, std::move(extra));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto(CallbackGroupPtr group, EventDtoPublisher::Options extra) {
        return create_publisher_eventdto(resolve_group(group), std::move(extra));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto_on(CallbackGroup&, EventDtoPublisher::Options extra) {
        return create_publisher_eventdto(std::move(extra));
    }

    std::unique_ptr<EventDtoPublisher> create_publisher_eventdto_on(CallbackGroupPtr, EventDtoPublisher::Options extra) {
        return create_publisher_eventdto(std::move(extra));
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(std::string topic,
                                                          std::size_t dto_max_payload = 8 * 1024,
                                                          wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        auto up = create_publisher_eventdto(std::move(topic), dto_max_payload, qos);
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(std::string topic,
                                                          CallbackGroup& group,
                                                          std::size_t dto_max_payload = 8 * 1024,
                                                          wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        auto up = create_publisher_eventdto(std::move(topic), group, dto_max_payload, std::move(qos));
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(std::string topic,
                                                          CallbackGroupPtr group,
                                                          std::size_t dto_max_payload = 8 * 1024,
                                                          wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto_shared(std::move(topic), resolve_group(group), dto_max_payload, std::move(qos));
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(CallbackGroup& group,
                                                          std::string topic,
                                                          std::size_t dto_max_payload = 8 * 1024,
                                                          wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        auto up = create_publisher_eventdto(group, std::move(topic), dto_max_payload, std::move(qos));
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(CallbackGroupPtr group,
                                                          std::string topic,
                                                          std::size_t dto_max_payload = 8 * 1024,
                                                          wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto_shared(resolve_group(group), std::move(topic), dto_max_payload, std::move(qos));
    }

    EventDtoPublisherPtr create_publisher_eventdto_on_shared(CallbackGroup& group,
                                                             std::string topic,
                                                             std::size_t dto_max_payload = 8 * 1024,
                                                             wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        auto up = create_publisher_eventdto_on(group, std::move(topic), dto_max_payload, std::move(qos));
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_on_shared(CallbackGroupPtr group,
                                                             std::string topic,
                                                             std::size_t dto_max_payload = 8 * 1024,
                                                             wxz::core::ChannelQoS qos = wxz::core::default_reliable_qos()) {
        return create_publisher_eventdto_on_shared(resolve_group(group),
                                                   std::move(topic),
                                                   dto_max_payload,
                                                   std::move(qos));
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(EventDtoPublisher::Options extra) {
        auto up = create_publisher_eventdto(std::move(extra));
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(EventDtoPublisher::Options extra, CallbackGroup& group) {
        auto up = create_publisher_eventdto(std::move(extra), group);
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(EventDtoPublisher::Options extra, CallbackGroupPtr group) {
        return create_publisher_eventdto_shared(std::move(extra), resolve_group(group));
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(CallbackGroup& group, EventDtoPublisher::Options extra) {
        auto up = create_publisher_eventdto(group, std::move(extra));
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_shared(CallbackGroupPtr group, EventDtoPublisher::Options extra) {
        return create_publisher_eventdto_shared(resolve_group(group), std::move(extra));
    }

    EventDtoPublisherPtr create_publisher_eventdto_on_shared(CallbackGroup& group, EventDtoPublisher::Options extra) {
        auto up = create_publisher_eventdto_on(group, std::move(extra));
        return EventDtoPublisherPtr(up.release());
    }

    EventDtoPublisherPtr create_publisher_eventdto_on_shared(CallbackGroupPtr group, EventDtoPublisher::Options extra) {
        return create_publisher_eventdto_on_shared(resolve_group(group), std::move(extra));
    }

    /// create_subscription<string>：适用于 KV 文本等非 EventDTO topic。
    std::unique_ptr<TextSubscription> create_subscription_text(std::string topic,
                                                               TextSubscription::Callback cb,
                                                               TextSubscription::Options extra = {}) {
        TextSubscription::Options opts = std::move(extra);
        opts.domain = base_.domain();
        opts.topic = std::move(topic);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        return std::make_unique<TextSubscription>(std::move(opts), *default_strand_, std::move(cb), logger_);
    }

    std::unique_ptr<TextSubscription> create_subscription_text(std::string topic,
                                                               TextSubscription::Callback cb,
                                                               CallbackGroup& group,
                                                               TextSubscription::Options extra = {}) {
        return create_subscription_text_on(group, std::move(topic), std::move(cb), std::move(extra));
    }

    std::unique_ptr<TextSubscription> create_subscription_text(std::string topic,
                                                               TextSubscription::Callback cb,
                                                               CallbackGroupPtr group,
                                                               TextSubscription::Options extra = {}) {
        return create_subscription_text(std::move(topic), std::move(cb), resolve_group(group), std::move(extra));
    }

    std::unique_ptr<TextSubscription> create_subscription_text(CallbackGroup& group,
                                                               std::string topic,
                                                               TextSubscription::Callback cb,
                                                               TextSubscription::Options extra = {}) {
        return create_subscription_text_on(group, std::move(topic), std::move(cb), std::move(extra));
    }

    std::unique_ptr<TextSubscription> create_subscription_text(CallbackGroupPtr group,
                                                               std::string topic,
                                                               TextSubscription::Callback cb,
                                                               TextSubscription::Options extra = {}) {
        return create_subscription_text(resolve_group(group), std::move(topic), std::move(cb), std::move(extra));
    }

    TextSubscriptionPtr create_subscription_text_shared(std::string topic,
                                                        TextSubscription::Callback cb,
                                                        TextSubscription::Options extra = {}) {
        auto up = create_subscription_text(std::move(topic), std::move(cb), std::move(extra));
        return TextSubscriptionPtr(up.release());
    }

    TextSubscriptionPtr create_subscription_text_shared(std::string topic,
                                                        TextSubscription::Callback cb,
                                                        CallbackGroup& group,
                                                        TextSubscription::Options extra = {}) {
        auto up = create_subscription_text(std::move(topic), std::move(cb), group, std::move(extra));
        return TextSubscriptionPtr(up.release());
    }

    TextSubscriptionPtr create_subscription_text_shared(std::string topic,
                                                        TextSubscription::Callback cb,
                                                        CallbackGroupPtr group,
                                                        TextSubscription::Options extra = {}) {
        return create_subscription_text_shared(std::move(topic), std::move(cb), resolve_group(group), std::move(extra));
    }

    TextSubscriptionPtr create_subscription_text_shared(CallbackGroup& group,
                                                        std::string topic,
                                                        TextSubscription::Callback cb,
                                                        TextSubscription::Options extra = {}) {
        auto up = create_subscription_text(group, std::move(topic), std::move(cb), std::move(extra));
        return TextSubscriptionPtr(up.release());
    }

    TextSubscriptionPtr create_subscription_text_shared(CallbackGroupPtr group,
                                                        std::string topic,
                                                        TextSubscription::Callback cb,
                                                        TextSubscription::Options extra = {}) {
        return create_subscription_text_shared(resolve_group(group), std::move(topic), std::move(cb), std::move(extra));
    }

    std::unique_ptr<TextSubscription> create_subscription_text_on(wxz::core::Strand& strand,
                                                                  std::string topic,
                                                                  TextSubscription::Callback cb,
                                                                  TextSubscription::Options extra = {}) {
        TextSubscription::Options opts = std::move(extra);
        opts.domain = base_.domain();
        opts.topic = std::move(topic);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        return std::make_unique<TextSubscription>(std::move(opts), strand, std::move(cb), logger_);
    }

    std::unique_ptr<TextSubscription> create_subscription_text_on(wxz::core::Executor& ex,
                                                                  std::string topic,
                                                                  TextSubscription::Callback cb,
                                                                  TextSubscription::Options extra = {}) {
        TextSubscription::Options opts = std::move(extra);
        opts.domain = base_.domain();
        opts.topic = std::move(topic);
        if (opts.metrics_scope.empty()) opts.metrics_scope = metrics_scope_;
        return std::make_unique<TextSubscription>(std::move(opts), ex, std::move(cb), logger_);
    }

    TextSubscriptionPtr create_subscription_text_on_shared(wxz::core::Strand& strand,
                                                           std::string topic,
                                                           TextSubscription::Callback cb,
                                                           TextSubscription::Options extra = {}) {
        auto up = create_subscription_text_on(strand, std::move(topic), std::move(cb), std::move(extra));
        return TextSubscriptionPtr(up.release());
    }

    TextSubscriptionPtr create_subscription_text_on_shared(wxz::core::Executor& ex,
                                                           std::string topic,
                                                           TextSubscription::Callback cb,
                                                           TextSubscription::Options extra = {}) {
        auto up = create_subscription_text_on(ex, std::move(topic), std::move(cb), std::move(extra));
        return TextSubscriptionPtr(up.release());
    }

    // TextSubscription 的 CallbackGroup 重载版本
    std::unique_ptr<TextSubscription> create_subscription_text_on(CallbackGroup& group,
                                                                  std::string topic,
                                                                  TextSubscription::Callback cb,
                                                                  TextSubscription::Options extra = {}) {
        if (group.strand()) return create_subscription_text_on(*group.strand(), std::move(topic), std::move(cb), std::move(extra));
        return create_subscription_text_on(*group.executor(), std::move(topic), std::move(cb), std::move(extra));
    }

    std::unique_ptr<TextSubscription> create_subscription_text_on(CallbackGroupPtr group,
                                                                  std::string topic,
                                                                  TextSubscription::Callback cb,
                                                                  TextSubscription::Options extra = {}) {
        return create_subscription_text_on(resolve_group(group), std::move(topic), std::move(cb), std::move(extra));
    }

    TextSubscriptionPtr create_subscription_text_on_shared(CallbackGroup& group,
                                                           std::string topic,
                                                           TextSubscription::Callback cb,
                                                           TextSubscription::Options extra = {}) {
        auto up = create_subscription_text_on(group, std::move(topic), std::move(cb), std::move(extra));
        return TextSubscriptionPtr(up.release());
    }

    TextSubscriptionPtr create_subscription_text_on_shared(CallbackGroupPtr group,
                                                           std::string topic,
                                                           TextSubscription::Callback cb,
                                                           TextSubscription::Options extra = {}) {
        auto up = create_subscription_text_on(resolve_group(group), std::move(topic), std::move(cb), std::move(extra));
        return TextSubscriptionPtr(up.release());
    }

    RpcServicePtr create_service_shared(std::string service,
                                        std::string sw_version,
                                        std::string request_topic = {},
                                        std::string reply_topic = {}) {
        auto up = create_service(std::move(service), std::move(sw_version), std::move(request_topic), std::move(reply_topic));
        return RpcServicePtr(up.release());
    }

    RpcServicePtr create_service_shared(std::string service,
                                        std::string sw_version,
                                        CallbackGroup& group,
                                        std::string request_topic = {},
                                        std::string reply_topic = {}) {
        auto up = create_service(std::move(service),
                                 std::move(sw_version),
                                 group,
                                 std::move(request_topic),
                                 std::move(reply_topic));
        return RpcServicePtr(up.release());
    }

    RpcServicePtr create_service_shared(std::string service,
                                        std::string sw_version,
                                        CallbackGroupPtr group,
                                        std::string request_topic = {},
                                        std::string reply_topic = {}) {
        return create_service_shared(std::move(service),
                                     std::move(sw_version),
                                     resolve_group(group),
                                     std::move(request_topic),
                                     std::move(reply_topic));
    }

    RpcServiceClientPtr create_client_shared(std::string service,
                                             std::string client_id_prefix = {},
                                             std::string request_topic = {},
                                             std::string reply_topic = {}) {
        auto up = create_client(std::move(service), std::move(client_id_prefix), std::move(request_topic), std::move(reply_topic));
        return RpcServiceClientPtr(up.release());
    }

    RpcServiceClientPtr create_client_shared(std::string service,
                                             CallbackGroup& group,
                                             std::string client_id_prefix = {},
                                             std::string request_topic = {},
                                             std::string reply_topic = {}) {
        auto up = create_client(std::move(service),
                                group,
                                std::move(client_id_prefix),
                                std::move(request_topic),
                                std::move(reply_topic));
        return RpcServiceClientPtr(up.release());
    }

    RpcServiceClientPtr create_client_shared(std::string service,
                                             CallbackGroupPtr group,
                                             std::string client_id_prefix = {},
                                             std::string request_topic = {},
                                             std::string reply_topic = {}) {
        return create_client_shared(std::move(service),
                                    resolve_group(group),
                                    std::move(client_id_prefix),
                                    std::move(request_topic),
                                    std::move(reply_topic));
    }

private:
    CallbackGroup& resolve_group(const CallbackGroupPtr& group) {
        return group ? *group : *default_callback_group_;
    }

    std::unique_ptr<wxz::core::Executor> owned_executor_;
    std::unique_ptr<wxz::core::Strand> owned_default_strand_;

    CallbackGroupPtr default_callback_group_;

    wxz::core::NodeBase base_;
    wxz::core::Executor* executor_{nullptr};
    wxz::core::Strand* default_strand_{nullptr};
    wxz::core::Logger* logger_{nullptr};
    std::string metrics_scope_;

    Parameters params_;
    TimerManager timers_;
};

} // namespace wxz::framework
