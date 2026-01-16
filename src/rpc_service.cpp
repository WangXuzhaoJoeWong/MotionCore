#include "rpc/rpc_service.h"

#include "clock.h"
#include "executor.h"
#include "fastdds_channel.h"
#include "observability.h"
#include "rpc/json_rpc.h"
#include "service_common.h"
#include "strand.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace wxz::core::rpc {

namespace {

inline std::string build_ok_response(std::string_view op,
                                    std::string_view id,
                                    std::uint64_t ts_ms,
                                    const nlohmann::json& result_obj) {
    nlohmann::json resp = nlohmann::json::object();
    resp["op"] = std::string(op);
    if (!id.empty()) resp["id"] = std::string(id);
    resp["status"] = "ok";
    resp["ts_ms"] = ts_ms;
    resp["result"] = result_obj;
    return resp.dump();
}

inline std::string build_error_response(std::string_view op,
                                       std::string_view id,
                                       std::uint64_t ts_ms,
                                       std::string_view reason) {
    nlohmann::json resp = nlohmann::json::object();
    resp["op"] = std::string(op);
    if (!id.empty()) resp["id"] = std::string(id);
    resp["status"] = "error";
    resp["ts_ms"] = ts_ms;
    resp["reason"] = std::string(reason);
    return resp.dump();
}

inline std::string_view json_get_string_view(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return {};
    // nlohmann::json 在内部保存 string；view 的生命周期绑定到对应的 json value。
    return it->get_ref<const std::string&>();
}

} // namespace

class RpcServer::Impl {
public:
    explicit Impl(RpcServerOptions opts)
        : opts_(std::move(opts)) {}

    struct InflightGuard {
        Impl* self{nullptr};
        explicit InflightGuard(Impl& impl) : self(&impl) {
            self->callbacks_inflight_.fetch_add(1, std::memory_order_relaxed);
        }
        ~InflightGuard() {
            if (!self) return;
            const auto prev = self->callbacks_inflight_.fetch_sub(1, std::memory_order_relaxed);
            if (prev == 1) {
                std::lock_guard<std::mutex> lk(self->cv_mu_);
                self->cv_.notify_all();
            }
        }
    };

    void bind_scheduler(Executor& ex) {
        std::lock_guard<std::mutex> lk(mu_);
        ex_ = &ex;
        strand_ = nullptr;
    }

    void bind_scheduler(Strand& strand) {
        std::lock_guard<std::mutex> lk(mu_);
        strand_ = &strand;
        ex_ = nullptr;
    }

    void add_handler(std::string op, Handler handler) {
        std::lock_guard<std::mutex> lk(mu_);
        handlers_.emplace(std::move(op), std::move(handler));
    }

    bool start() {
        std::lock_guard<std::mutex> lk(mu_);
        if (started_) return true;
        if (opts_.request_topic.empty() || opts_.reply_topic.empty()) return false;

        stopping_.store(false, std::memory_order_relaxed);

        const ChannelQoS qos = opts_.qos;
        req_.emplace(opts_.domain, opts_.request_topic, qos, 8192, /*pub*/ false, /*sub*/ true);
        rep_.emplace(opts_.domain, opts_.reply_topic, qos, 8192, /*pub*/ true, /*sub*/ false);

        auto cb = [this](const std::uint8_t* data, std::size_t size) {
            this->on_request(data, size);
        };

        // 默认：直接 subscribe（回调在 FastDDS listener 线程）。若绑定了调度器，则使用 subscribe_on。
        if (strand_) {
            req_->subscribe_on(*strand_, cb);
        } else if (ex_) {
            req_->subscribe_on(*ex_, cb);
        } else {
            req_->subscribe(cb);
        }

        started_ = true;
        return true;
    }

    void stop() {
        // Teardown 必须安全：即便 executor/strand 线程上仍有回调正在执行。
        // - 首先阻止新工作进入（停止 request 订阅）。
        // - 然后等待 in-flight 回调退出。
        // - 最后停止/重置 reply publisher。
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!started_) return;
            stopping_.store(true, std::memory_order_relaxed);

            if (req_) req_->stop();
            req_.reset();
        }

        {
            std::unique_lock<std::mutex> lk(cv_mu_);
            cv_.wait_for(lk, std::chrono::seconds(3), [&] {
                return callbacks_inflight_.load(std::memory_order_relaxed) == 0;
            });
        }

        {
            std::lock_guard<std::mutex> lk(mu_);
            if (rep_) rep_->stop();
            rep_.reset();
            started_ = false;
            stopping_.store(false, std::memory_order_relaxed);
        }
    }

    void on_request(const std::uint8_t* data, std::size_t size) {
        if (stopping_.load(std::memory_order_relaxed)) return;
        InflightGuard guard(*this);

        const std::uint64_t ts_server_ms = now_epoch_ms();

        if (has_metrics_sink()) {
            metrics().counter_add("wxz.rpc.server.request_total", 1,
                                  {{"scope", opts_.metrics_scope}, {"service", opts_.service_name}, {"topic", opts_.request_topic}});
        }

        const std::string_view text(reinterpret_cast<const char*>(data), size);
        auto parsed = parseJsonObject(text);
        if (!parsed) {
            publish_error("", "", ts_server_ms, "parse_error");
            return;
        }

        const auto& obj = *parsed;
        const std::string_view op = json_get_string_view(obj, "op");
        const std::string_view id = json_get_string_view(obj, "id");
        if (op.empty()) {
            publish_error("", id, ts_server_ms, "missing_op");
            return;
        }

        nlohmann::json params = nlohmann::json::object();
        auto it_params = obj.find("params");
        if (it_params != obj.end() && it_params->is_object()) {
            params = *it_params;
        }

        Handler handler;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = handlers_.find(std::string(op));
            if (it != handlers_.end()) {
                handler = it->second;
            }
        }

        if (!handler) {
            publish_error(op, id, ts_server_ms, "unknown_op");
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        Reply reply;
        try {
            reply = handler(params);
        } catch (...) {
            reply.ok = false;
            reply.reason = "handler_exception";
            reply.result = nlohmann::json::object();
        }
        const auto end = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (has_metrics_sink()) {
            metrics().histogram_observe("wxz.rpc.server.handler_ms", static_cast<double>(ms),
                                       {{"scope", opts_.metrics_scope}, {"service", opts_.service_name}, {"op", op}});
            if (!reply.ok) {
                metrics().counter_add("wxz.rpc.server.error_total", 1,
                                      {{"scope", opts_.metrics_scope}, {"service", opts_.service_name}, {"op", op}});
            }
        }

        if (reply.ok) {
            const std::string resp = build_ok_response(op, id, ts_server_ms, reply.result);
            (void)rep_->publish(reinterpret_cast<const std::uint8_t*>(resp.data()), resp.size());
        } else {
            const std::string resp = build_error_response(op, id, ts_server_ms, reply.reason);
            (void)rep_->publish(reinterpret_cast<const std::uint8_t*>(resp.data()), resp.size());
        }
    }

    void publish_error(std::string_view op, std::string_view id, std::uint64_t ts_ms, std::string_view reason) {
        if (stopping_.load(std::memory_order_relaxed)) return;
        if (!rep_) return;
        if (has_metrics_sink()) {
            metrics().counter_add("wxz.rpc.server.error_total", 1,
                                  {{"scope", opts_.metrics_scope}, {"service", opts_.service_name}, {"op", op.empty() ? "" : op}});
        }
        const std::string resp = build_error_response(op, id, ts_ms, reason);
        (void)rep_->publish(reinterpret_cast<const std::uint8_t*>(resp.data()), resp.size());
    }

    RpcServerOptions opts_;

    mutable std::mutex mu_;
    bool started_{false};

    Executor* ex_{nullptr};
    Strand* strand_{nullptr};

    std::unordered_map<std::string, Handler> handlers_;

    std::optional<FastddsChannel> req_;
    std::optional<FastddsChannel> rep_;

    // 停止安全：允许 stop() 等待直到没有回调在使用 rep_。
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint32_t> callbacks_inflight_{0};
    std::mutex cv_mu_;
    std::condition_variable cv_;
};

RpcServer::RpcServer(RpcServerOptions opts) : impl_(std::make_unique<Impl>(std::move(opts))) {}
RpcServer::~RpcServer() = default;

void RpcServer::bind_scheduler(Executor& ex) { impl_->bind_scheduler(ex); }
void RpcServer::bind_scheduler(Strand& strand) { impl_->bind_scheduler(strand); }

void RpcServer::add_handler(std::string op, Handler handler) { impl_->add_handler(std::move(op), std::move(handler)); }

bool RpcServer::start() { return impl_->start(); }
void RpcServer::stop() { impl_->stop(); }

} // namespace wxz::core::rpc
