#include "rpc/rpc_client.h"

#include "executor.h"
#include "fastdds_channel.h"
#include "observability.h"
#include "rpc/json_rpc.h"
#include "service_common.h"
#include "strand.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace wxz::core::rpc {

namespace {

inline std::string build_request(const std::string& op,
                                 const std::string& id,
                                 std::uint64_t ts_ms,
                                 const nlohmann::json& params_obj) {
    nlohmann::json req = nlohmann::json::object();
    req["op"] = op;
    req["id"] = id;
    req["ts_ms"] = ts_ms;
    req["params"] = params_obj;
    return req.dump();
}

inline std::string_view json_get_string_view(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return {};
    return it->get_ref<const std::string&>();
}

} // namespace

class RpcClient::Impl {
public:
    explicit Impl(RpcClientOptions opts)
        : opts_(std::move(opts)) {}

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

    bool start() {
        std::lock_guard<std::mutex> lk(mu_);
        if (started_) return true;
        if (opts_.request_topic.empty() || opts_.reply_topic.empty()) return false;

        const ChannelQoS qos = opts_.qos;
        req_pub_.emplace(opts_.domain, opts_.request_topic, qos, 8192, /*pub*/ true, /*sub*/ false);
        rep_sub_.emplace(opts_.domain, opts_.reply_topic, qos, 8192, /*pub*/ false, /*sub*/ true);

        auto cb = [this](const std::uint8_t* data, std::size_t size) {
            this->on_reply(data, size);
        };

        if (strand_) {
            rep_sub_->subscribe_on(*strand_, cb);
        } else if (ex_) {
            rep_sub_->subscribe_on(*ex_, cb);
        } else {
            rep_sub_->subscribe(cb);
        }

        started_ = true;
        return true;
    }

    void stop() {
        std::unordered_map<std::string, std::shared_ptr<std::promise<Result>>> to_cancel;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!started_) return;

            // Best-effort: cancel all pending.
            for (auto& [id, p] : pending_) {
                to_cancel.emplace(id, p.promise);
            }
            pending_.clear();

            if (req_pub_) req_pub_->stop();
            if (rep_sub_) rep_sub_->stop();
            req_pub_.reset();
            rep_sub_.reset();
            started_ = false;
        }

        for (auto& [id, pr] : to_cancel) {
            Result r;
            r.code = RpcErrorCode::Cancelled;
            r.reason = "client_stopped";
            try {
                pr->set_value(std::move(r));
            } catch (...) {
            }
        }
    }

    Result call(const std::string& op, const nlohmann::json& params, std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0) timeout = std::chrono::milliseconds(1);

        std::shared_ptr<std::promise<Result>> pr = std::make_shared<std::promise<Result>>();
        auto fut = pr->get_future();

        const std::uint64_t ts_ms = now_epoch_ms();
        const auto start_steady = std::chrono::steady_clock::now();

        std::string id;
        {
            const auto seq = next_id_.fetch_add(1, std::memory_order_relaxed);
            if (!opts_.client_id_prefix.empty()) {
                id = opts_.client_id_prefix + "-" + std::to_string(seq);
            } else {
                id = std::to_string(seq);
            }
        }

        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!started_ || !req_pub_) {
                Result r;
                r.code = RpcErrorCode::NotStarted;
                r.reason = "client_not_started";
                return r;
            }
            pending_.emplace(id, Pending{op, start_steady, pr});
        }

        if (has_metrics_sink()) {
            metrics().counter_add("wxz.rpc.client.request_total", 1,
                                  {{"scope", opts_.metrics_scope}, {"topic", opts_.request_topic}, {"op", op}});
            metrics().gauge_set("wxz.rpc.client.pending", static_cast<double>(pending_size()),
                               {{"scope", opts_.metrics_scope}, {"topic", opts_.request_topic}});
        }

        const std::string req = build_request(op, id, ts_ms, params);
        const bool ok = req_pub_->publish(reinterpret_cast<const std::uint8_t*>(req.data()), req.size());
        if (!ok) {
            erase_pending(id);
            Result r;
            r.code = RpcErrorCode::TransportError;
            r.reason = "publish_failed";
            if (has_metrics_sink()) {
                metrics().counter_add("wxz.rpc.client.error_total", 1,
                                      {{"scope", opts_.metrics_scope}, {"topic", opts_.request_topic}, {"op", op}, {"code", to_string(r.code)}});
            }
            return r;
        }

        if (fut.wait_for(timeout) == std::future_status::timeout) {
            erase_pending(id);
            Result r;
            r.code = RpcErrorCode::Timeout;
            r.reason = "timeout";
            if (has_metrics_sink()) {
                metrics().counter_add("wxz.rpc.client.error_total", 1,
                                      {{"scope", opts_.metrics_scope}, {"topic", opts_.request_topic}, {"op", op}, {"code", to_string(r.code)}});
            }
            return r;
        }

        Result r = fut.get();
        if (has_metrics_sink()) {
            const auto end = std::chrono::steady_clock::now();
            const auto rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_steady).count();
            metrics().histogram_observe("wxz.rpc.client.rtt_ms", static_cast<double>(rtt_ms),
                               {{"scope", opts_.metrics_scope}, {"topic", opts_.request_topic}, {"op", op}, {"code", to_string(r.code)}});
            metrics().gauge_set("wxz.rpc.client.pending", static_cast<double>(pending_size()),
                           {{"scope", opts_.metrics_scope}, {"topic", opts_.request_topic}});
        }
        return r;
    }

    void on_reply(const std::uint8_t* data, std::size_t size) {
        const std::string_view text(reinterpret_cast<const char*>(data), size);
        auto parsed = parseJsonObject(text);
        if (!parsed) {
            if (has_metrics_sink()) {
                metrics().counter_add("wxz.rpc.client.reply_drop_total", 1,
                                      {{"scope", opts_.metrics_scope}, {"topic", opts_.reply_topic}, {"reason", "parse_error"}});
            }
            return;
        }

        const auto& obj = *parsed;
        const std::string_view id = json_get_string_view(obj, "id");
        if (id.empty()) {
            if (has_metrics_sink()) {
                metrics().counter_add("wxz.rpc.client.reply_drop_total", 1,
                                      {{"scope", opts_.metrics_scope}, {"topic", opts_.reply_topic}, {"reason", "missing_id"}});
            }
            return;
        }

        std::shared_ptr<std::promise<Result>> pr;
        std::string op;
        std::chrono::steady_clock::time_point start_steady;

        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = pending_.find(std::string(id));
            if (it == pending_.end()) {
                if (has_metrics_sink()) {
                    metrics().counter_add("wxz.rpc.client.reply_drop_total", 1,
                                          {{"scope", opts_.metrics_scope}, {"topic", opts_.reply_topic}, {"reason", "unknown_id"}});
                }
                return;
            }
            op = it->second.op;
            start_steady = it->second.start_steady;
            pr = it->second.promise;
            pending_.erase(it);
        }

        Result r;
        const std::string_view status = json_get_string_view(obj, "status");
        if (status == "ok") {
            r.code = RpcErrorCode::Ok;
            auto it_res = obj.find("result");
            if (it_res != obj.end()) {
                r.result = *it_res;
            }
        } else if (status == "error") {
            r.code = RpcErrorCode::RemoteError;
            auto it_reason = obj.find("reason");
            if (it_reason != obj.end() && it_reason->is_string()) {
                r.reason = it_reason->get<std::string>();
            } else {
                r.reason = "remote_error";
            }
        } else {
            r.code = RpcErrorCode::ParseError;
            r.reason = "invalid_status";
        }

        try {
            pr->set_value(r);
        } catch (...) {
        }

        if (has_metrics_sink()) {
            const auto end = std::chrono::steady_clock::now();
            const auto rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_steady).count();
            metrics().histogram_observe("wxz.rpc.client.rtt_ms", static_cast<double>(rtt_ms),
                                       {{"scope", opts_.metrics_scope}, {"topic", opts_.request_topic}, {"op", op}, {"code", to_string(r.code)}});
        }
    }

    std::size_t pending_size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return pending_.size();
    }

    void erase_pending(const std::string& id) {
        std::lock_guard<std::mutex> lk(mu_);
        pending_.erase(id);
    }

    struct Pending {
        std::string op;
        std::chrono::steady_clock::time_point start_steady;
        std::shared_ptr<std::promise<Result>> promise;
    };

    RpcClientOptions opts_;

    mutable std::mutex mu_;
    bool started_{false};

    Executor* ex_{nullptr};
    Strand* strand_{nullptr};

    std::optional<FastddsChannel> req_pub_;
    std::optional<FastddsChannel> rep_sub_;

    std::unordered_map<std::string, Pending> pending_;
    std::atomic<std::uint64_t> next_id_{1};
};

RpcClient::RpcClient(RpcClientOptions opts) : impl_(std::make_unique<Impl>(std::move(opts))) {}
RpcClient::~RpcClient() = default;

void RpcClient::bind_scheduler(Executor& ex) { impl_->bind_scheduler(ex); }
void RpcClient::bind_scheduler(Strand& strand) { impl_->bind_scheduler(strand); }

bool RpcClient::start() { return impl_->start(); }
void RpcClient::stop() { impl_->stop(); }

RpcClient::Result RpcClient::call(const std::string& op, const Json& params, std::chrono::milliseconds timeout) {
    return impl_->call(op, params, timeout);
}

} // namespace wxz::core::rpc
