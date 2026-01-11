#include "internal/param_server.h"
#include "internal/param_store.h"
#include "logger.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <cstdlib>

namespace {

long long nowEpochMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string json_escape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (const char c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            // Keep it simple: only escape common control chars.
            if (static_cast<unsigned char>(c) < 0x20) {
                out += ' ';
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

// Parse a very small subset of "k=v;k2=v2" payloads.
// Returns empty string if missing.
std::string kv_get(std::string_view msg, std::string_view key) {
    const std::string needle = std::string(key) + "=";
    const auto pos = msg.find(needle);
    if (pos == std::string_view::npos) return "";
    std::size_t start = pos + needle.size();
    std::size_t end = msg.find(';', start);
    if (end == std::string_view::npos) end = msg.size();
    return std::string(msg.substr(start, end - start));
}

bool looks_like_export_request(std::string_view msg) {
    if (msg.empty()) return false;
    if (msg == "param.export") return true;
    if (msg.rfind("EXPORT", 0) == 0) return true;
    if (msg.find("op=param.export") != std::string_view::npos) return true;
    return false;
}

} // namespace

namespace wxz::core::internal {

ParamServer::ParamServer(int domain_id,
                         std::string set_topic,
                         std::string ack_topic)
    : domain_id_(domain_id), set_topic_(std::move(set_topic)), ack_topic_(std::move(ack_topic)) {}

ParamServer::~ParamServer() {
    stop();
}

void ParamServer::declare(const std::string& name, const std::string& default_val, Callback cb) {
    params_[name] = default_val;
    callbacks_[name] = std::move(cb);
}

void ParamServer::setSchema(const std::string& name, ParamSpec spec) {
    schemas_[name] = std::move(spec);
}

void ParamServer::applyBulk(const std::unordered_map<std::string, std::string>& kvs) {
    for (const auto& [key, val] : kvs) {
        validateAndApply(key, val, false);
    }
    persistIfConfigured();
}

std::unordered_map<std::string, std::string> ParamServer::exportAll() const {
    return params_;
}

std::string ParamServer::exportAllJson() const {
    std::string out;
    out.reserve(params_.size() * 16);
    out.push_back('{');
    bool first = true;
    for (const auto& [k, v] : params_) {
        if (!first) out.push_back(',');
        first = false;
        out.push_back('"');
        out += json_escape(k);
        out += "\":\"";
        out += json_escape(v);
        out.push_back('"');
    }
    out.push_back('}');
    return out;
}

void ParamServer::setSnapshotPath(std::string path) {
    snapshot_path_ = std::move(path);
}

void ParamServer::loadSnapshot() {
    if (snapshot_path_.empty()) return;
    std::ifstream ifs(snapshot_path_);
    if (!ifs.good()) return;
    std::unordered_map<std::string, std::string> kvs;
    if (snapshot_path_.size() >= 5 && snapshot_path_.substr(snapshot_path_.size() - 5) == ".json") {
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        std::string key, val;
        bool in_key = false, in_val = false;
        for (size_t i = 0; i < content.size(); ++i) {
            char c = content[i];
            if (c == '"') {
                if (!in_key && !in_val) {
                    in_key = true;
                    key.clear();
                } else if (in_key && !in_val) {
                    in_key = false;
                } else if (in_val) {
                    in_val = false;
                    kvs[key] = val;
                }
            } else if (in_key) {
                key.push_back(c);
            } else if (c == ':' && !in_val) {
                in_val = true; val.clear();
            } else if (in_val && c != '"' && c != ',' && c != '{' && c != '}' && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
                val.push_back(c);
            }
        }
    } else {
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            kvs[key] = val;
        }
    }
    applyBulk(kvs);
}

void ParamServer::saveSnapshot() {
    if (snapshot_path_.empty()) return;
    std::ofstream ofs(snapshot_path_, std::ios::trunc);
    if (!ofs.good()) {
        wxz::core::Logger::getInstance().warn(std::string("ParamServer snapshot write failed: ") + snapshot_path_);
        return;
    }
    if (snapshot_path_.size() >= 5 && snapshot_path_.substr(snapshot_path_.size() - 5) == ".json") {
        ofs << "{";
        bool first = true;
        for (const auto& [k, v] : params_) {
            if (!first) ofs << ','; else first = false;
            ofs << "\"" << k << "\":\"" << v << "\"";
        }
        ofs << "}";
    } else {
        for (const auto& [k, v] : params_) {
            ofs << k << '=' << v << '\n';
        }
    }
    wxz::core::Logger::getInstance().info(std::string("ParamServer snapshot saved: ") + snapshot_path_);
}

void ParamServer::setFetchCallback(FetchCallback cb, std::chrono::milliseconds interval) {
    fetch_cb_ = std::move(cb);
    fetch_interval_ = interval;
    maybeStartFetchThread();
}

void ParamServer::setHttpFetch(const std::string& url, std::chrono::milliseconds interval) {
    fetch_cb_ = [url]() { return fetch_kv_over_http(url); };
    fetch_interval_ = interval;
    maybeStartFetchThread();
}

void ParamServer::setHttpFetchList(const std::vector<std::string>& urls, std::chrono::milliseconds interval) {
    fetch_cb_ = [urls]() {
        std::unordered_map<std::string, std::string> merged;
        for (const auto& u : urls) {
            auto kvs = fetch_kv_over_http(u);
            merged.insert(kvs.begin(), kvs.end());
        }
        return merged;
    };
    fetch_interval_ = interval;
    maybeStartFetchThread();
}

void ParamServer::maybeStartFetchThread() {
    if (!running_) {
        return;
    }
    if (!fetch_cb_ || fetch_interval_.count() <= 0) {
        return;
    }
    bool expected = false;
    if (!fetch_running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }
    if (fetch_thread_.joinable()) {
        fetch_thread_.join();
    }
    fetch_thread_ = std::thread(&ParamServer::fetchLoop, this);
}

void ParamServer::setExportTopics(std::string request_topic, std::string reply_topic) {
    export_request_topic_ = std::move(request_topic);
    export_reply_topic_ = std::move(reply_topic);
}

void ParamServer::start() {
    if (running_) return;
    running_ = true;

    ensureChannelsStarted();
    ensureExportChannelsStarted();

    worker_ = std::thread(&ParamServer::loop, this);
    maybeStartFetchThread();
}

void ParamServer::stop() {
    running_ = false;
    fetch_running_ = false;
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (fetch_thread_.joinable()) fetch_thread_.join();

    // Best-effort: stop subscriptions first to avoid callbacks racing teardown.
    {
        std::lock_guard<std::mutex> lock(channel_mu_);
        set_subscription_.reset();
        export_subscription_.reset();
        if (set_sub_) set_sub_->stop();
        if (ack_pub_) ack_pub_->stop();
        if (export_req_sub_) export_req_sub_->stop();
        if (export_reply_pub_) export_reply_pub_->stop();
        set_sub_.reset();
        ack_pub_.reset();
        export_req_sub_.reset();
        export_reply_pub_.reset();
    }
}

void ParamServer::ensureChannelsStarted() {
    std::lock_guard<std::mutex> lock(channel_mu_);
    if (!set_sub_) {
        set_sub_ = std::make_unique<wxz::core::FastddsChannel>(
            domain_id_, set_topic_, qos_, max_payload_, /*enable_pub=*/false, /*enable_sub=*/true);
        set_subscription_ = set_sub_->subscribe_scoped(
            [this](const std::uint8_t* data, std::size_t size) {
                if (!data || size == 0) return;
                if (!running_.load(std::memory_order_relaxed)) return;
                std::string msg(reinterpret_cast<const char*>(data), size);
                {
                    std::lock_guard<std::mutex> ql(queue_mu_);
                    set_queue_.push_back(std::move(msg));
                    while (set_queue_.size() > 64) {
                        set_queue_.pop_front();
                    }
                }
                queue_cv_.notify_one();
            },
            this);
    }

    if (!ack_pub_) {
        ack_pub_ = std::make_unique<wxz::core::FastddsChannel>(
            domain_id_, ack_topic_, qos_, max_payload_, /*enable_pub=*/true, /*enable_sub=*/false);
    }
}

void ParamServer::ensureExportChannelsStarted() {
    std::lock_guard<std::mutex> lock(channel_mu_);
    if (export_request_topic_.empty()) {
        return;
    }

    // Refresh subscription/publish channels if topics changed.
    const auto reply_topic = export_reply_topic_.empty() ? export_request_topic_ : export_reply_topic_;

    if (!export_req_sub_ || !export_reply_pub_) {
        export_subscription_.reset();
        if (export_req_sub_) export_req_sub_->stop();
        if (export_reply_pub_) export_reply_pub_->stop();
        export_req_sub_.reset();
        export_reply_pub_.reset();

        export_req_sub_ = std::make_unique<wxz::core::FastddsChannel>(
            domain_id_, export_request_topic_, qos_, max_payload_, /*enable_pub=*/false, /*enable_sub=*/true);
        export_reply_pub_ = std::make_unique<wxz::core::FastddsChannel>(
            domain_id_, reply_topic, qos_, max_payload_, /*enable_pub=*/true, /*enable_sub=*/false);

        export_subscription_ = export_req_sub_->subscribe_scoped(
            [this](const std::uint8_t* data, std::size_t size) {
                if (!data || size == 0) return;
                if (!running_.load(std::memory_order_relaxed)) return;
                std::string msg(reinterpret_cast<const char*>(data), size);
                {
                    std::lock_guard<std::mutex> ql(queue_mu_);
                    export_queue_.push_back(std::move(msg));
                    while (export_queue_.size() > 64) {
                        export_queue_.pop_front();
                    }
                }
                queue_cv_.notify_one();
            },
            this);
    }
}

void ParamServer::publishOnAckTopic(const std::string& payload) {
    std::lock_guard<std::mutex> lock(channel_mu_);
    if (!ack_pub_) return;
    (void)ack_pub_->publish(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
}

void ParamServer::publishOnExportReplyTopic(const std::string& payload) {
    std::lock_guard<std::mutex> lock(channel_mu_);
    if (!export_reply_pub_) return;
    (void)export_reply_pub_->publish(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
}

void ParamServer::loop() {
    using namespace std::chrono_literals;
    loop_entered_.store(true);
    while (running_) {
        try {
            // Ensure export channels are started if enableExportService() was called after start().
            if (!export_request_topic_.empty()) {
                ensureExportChannelsStarted();
            }

            std::deque<std::string> export_msgs;
            std::deque<std::string> set_msgs;
            {
                std::unique_lock<std::mutex> lock(queue_mu_);
                queue_cv_.wait_for(lock, 50ms, [this]() {
                    return !running_.load(std::memory_order_relaxed) || !export_queue_.empty() || !set_queue_.empty();
                });

                export_msgs.swap(export_queue_);
                set_msgs.swap(set_queue_);
            }

            for (const auto& dump_req : export_msgs) {
                if (!running_.load(std::memory_order_relaxed)) break;
                if (dump_req.empty()) continue;
                const auto ts_ms = nowEpochMs();
                // Prevent self-triggering loops if request/reply share the same topic.
                if (dump_req.rfind("BULK", 0) == 0) continue;
                if (dump_req.find("status=") != std::string::npos) continue;

                if (!looks_like_export_request(dump_req)) {
                    continue;
                }

                const std::string id = kv_get(dump_req, "id");

                // Reply in the same lightweight BULK format used by set messages.
                // NOTE: This is intended for debug tooling; values are not escaped.
                std::string payload;
                payload.reserve(64 + params_.size() * 16);
                payload += "BULK ";
                std::size_t count = 0;
                for (const auto& [k, v] : params_) {
                    if (!payload.empty() && payload.back() != ' ') payload.push_back(';');
                    payload += k;
                    payload.push_back('=');
                    payload += v;
                    ++count;
                    if (payload.size() > max_payload_ - 128) break;
                }

                // Append minimal metadata (best-effort) for correlation.
                payload += ";op=param.export";
                if (!id.empty()) {
                    payload += ";id=";
                    payload += id;
                }
                payload += ";ts_ms=";
                payload += std::to_string(ts_ms);
                payload += ";count=";
                payload += std::to_string(count);

                publishOnExportReplyTopic(payload);
            }

            for (const auto& msg : set_msgs) {
                if (!running_.load(std::memory_order_relaxed)) break;
                if (msg.empty()) continue;

                // Bulk format: BULK key1=val1;key2=val2
                if (msg.rfind("BULK", 0) == 0) {
                    auto body_pos = msg.find(' ');
                    if (body_pos != std::string::npos && body_pos + 1 < msg.size()) {
                        handleBulkMessage(msg.substr(body_pos + 1));
                    }
                } else {
                    handleSetMessage(msg);
                }
            }
        } catch (const std::exception& e) {
            wxz::core::Logger::getInstance().error(std::string("ParamServer error: ") + e.what());
            std::this_thread::sleep_for(200ms);
        }
    }
}

void ParamServer::handleSetMessage(const std::string& msg) {
    auto eq = msg.find('=');
    if (eq == std::string::npos) return;
    std::string key = msg.substr(0, eq);
    std::string val = msg.substr(eq + 1);
    validateAndApply(key, val, true);
    persistIfConfigured();
}

void ParamServer::handleBulkMessage(const std::string& body) {
    // Expect semicolon-separated key=val list; blanks ignored.
    std::unordered_map<std::string, std::string> applied;
    size_t start = 0;
    while (start < body.size()) {
        size_t sep = body.find(';', start);
        std::string token = body.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        if (!token.empty()) {
            auto eq = token.find('=');
            if (eq != std::string::npos) {
                std::string key = token.substr(0, eq);
                std::string val = token.substr(eq + 1);
                if (validateAndApply(key, val, false)) {
                    applied[key] = val;
                }
            }
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    // Ack with simple JSON map
    if (!applied.empty()) {
        std::ostringstream oss;
        oss << "{\"status\":\"ok\",\"applied\":{";
        bool first = true;
        for (const auto& [k, v] : applied) {
            if (!first) oss << ','; else first = false;
            oss << "\"" << k << "\":\"" << v << "\"";
        }
        oss << "}}";
        publishOnAckTopic(oss.str());
    }
    persistIfConfigured();
}

bool ParamServer::typeAccepts(const std::string& type, const std::string& val) const {
    if (type.empty() || type == "string") return true;
    if (type == "bool") {
        return val == "true" || val == "false" || val == "0" || val == "1";
    }
    if (type == "int") {
        char* end = nullptr;
        std::strtol(val.c_str(), &end, 10);
        return end && *end == '\0';
    }
    if (type == "double") {
        char* end = nullptr;
        std::strtod(val.c_str(), &end);
        return end && *end == '\0';
    }
    return false;
}

bool ParamServer::validateAndApply(const std::string& key, const std::string& val, bool send_ack) {
    if (auto it = schemas_.find(key); it != schemas_.end()) {
        if (it->second.read_only && params_.count(key)) {
            if (send_ack) sendAckError(key, "read_only");
            wxz::core::Logger::getInstance().warn(std::string("ParamServer reject read_only key=") + key + " metric=param.validation_fail");
            return false;
        }
        if (!typeAccepts(it->second.type, val)) {
            if (send_ack) sendAckError(key, "type_mismatch");
            wxz::core::Logger::getInstance().warn(std::string("ParamServer type_mismatch key=") + key + " val=" + val + " expected=" + it->second.type + " metric=param.validation_fail");
            return false;
        }
    }

    params_[key] = val;
    ParamStore::instance().set(key, val);
    if (auto it = callbacks_.find(key); it != callbacks_.end()) {
        it->second(key, val);
    }
    if (send_ack) sendAckOk(key, val);
    return true;
}

void ParamServer::sendAckOk(const std::string& key, const std::string& val) {
    std::ostringstream oss;
    oss << "{\"name\":\"" << key << "\",\"status\":\"ok\",\"value\":\"" << val << "\"}";
    publishOnAckTopic(oss.str());
}

void ParamServer::sendAckError(const std::string& key, const std::string& err) {
    std::ostringstream oss;
    oss << "{\"name\":\"" << key << "\",\"status\":\"error\",\"reason\":\"" << err << "\"}";
    publishOnAckTopic(oss.str());
}

void ParamServer::persistIfConfigured() {
    if (!snapshot_path_.empty()) {
        saveSnapshot();
    }
}

void ParamServer::fetchLoop() {
    using namespace std::chrono_literals;
    while (fetch_running_) {
        try {
            if (fetch_cb_) {
                auto kvs = fetch_cb_();
                if (!kvs.empty()) {
                    applyBulk(kvs);
                }
            }
        } catch (const std::exception& e) {
            wxz::core::Logger::getInstance().error(std::string("ParamServer fetch error: ") + e.what());
        }
        std::this_thread::sleep_for(fetch_interval_.count() > 0 ? fetch_interval_ : 1000ms);
    }
}

} // namespace wxz::core::internal
