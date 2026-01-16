#include "internal/discovery.h"

#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <unistd.h>

DiscoveryClient::~DiscoveryClient() { stop(); }

void DiscoveryClient::start(const std::string& endpoint,
                            int heartbeat_period_ms,
                            int ttl_ms,
                            const std::string& node_role,
                            const std::string& node_zone,
                            const std::vector<std::string>& node_endpoints) {
    if (running_) return;
    if (endpoint.empty() || heartbeat_period_ms <= 0 || ttl_ms <= 0) return;

    endpoint_ = endpoint;
    period_ms_ = heartbeat_period_ms;
    ttl_ms_ = ttl_ms;
    node_role_ = node_role;
    node_zone_ = node_zone;
    node_endpoints_ = node_endpoints;

    char hn[256] = {0};
    if (gethostname(hn, sizeof(hn) - 1) == 0) {
        hostname_ = hn;
    }

    running_ = true;
    worker_ = std::thread([this]() { run(); });
}

void DiscoveryClient::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_.joinable()) worker_.join();
    // 优雅下线：补发一次心跳，再尝试注销。
    (void)sendHeartbeat();
    // 尽力而为：尝试注销一次。
    (void)sendDeregister();
}

void DiscoveryClient::run() {
    // 尝试进行一次初始注册。
    if (!sendRegister()) {
        std::cerr << "[discovery] initial register failed to " << endpoint_ << "\n";
    }
    while (running_) {
        const bool ok = sendHeartbeat();
        if (!ok) {
            std::cerr << "[discovery] heartbeat failed to " << endpoint_ << "\n";
        }
        // 机会性地刷新 peer 列表；失败不致命。
        (void)fetchPeers();
        std::this_thread::sleep_for(std::chrono::milliseconds(period_ms_));
    }
}

bool DiscoveryClient::sendHeartbeat() {
    std::ostringstream payload;
    payload << "{\"kind\":\"heartbeat\",";
    payload << "\"role\":\"" << node_role_ << "\",";
    if (!node_zone_.empty()) {
        payload << "\"zone\":\"" << node_zone_ << "\",";
    }
    payload << "\"ttl_ms\":" << ttl_ms_ << ',';
    if (!hostname_.empty()) {
        payload << "\"hostname\":\"" << hostname_ << "\",";
    }
    payload << "\"endpoints\":[";
    for (size_t i = 0; i < node_endpoints_.size(); ++i) {
        if (i) payload << ',';
        payload << "\"" << node_endpoints_[i] << "\"";
    }
    payload << "]}";
    return postJson(payload.str(), "heartbeat");
}

bool DiscoveryClient::sendRegister() {
    std::ostringstream payload;
    payload << "{\"kind\":\"register\",";
    payload << "\"role\":\"" << node_role_ << "\",";
    if (!node_zone_.empty()) {
        payload << "\"zone\":\"" << node_zone_ << "\",";
    }
    payload << "\"ttl_ms\":" << ttl_ms_ << ',';
    if (!hostname_.empty()) {
        payload << "\"hostname\":\"" << hostname_ << "\",";
    }
    payload << "\"endpoints\":[";
    for (size_t i = 0; i < node_endpoints_.size(); ++i) {
        if (i) payload << ',';
        payload << "\"" << node_endpoints_[i] << "\"";
    }
    payload << "]}";
    return postJson(payload.str(), "register");
}

bool DiscoveryClient::sendDeregister() {
    std::ostringstream payload;
    payload << "{\"kind\":\"deregister\",";
    payload << "\"role\":\"" << node_role_ << "\",";
    if (!node_zone_.empty()) {
        payload << "\"zone\":\"" << node_zone_ << "\",";
    }
    if (!hostname_.empty()) {
        payload << "\"hostname\":\"" << hostname_ << "\",";
    }
    payload << "\"endpoints\":[";
    for (size_t i = 0; i < node_endpoints_.size(); ++i) {
        if (i) payload << ',';
        payload << "\"" << node_endpoints_[i] << "\"";
    }
    payload << "]}";
    return postJson(payload.str(), "deregister");
}

bool DiscoveryClient::postJson(const std::string& payload, const char* purpose) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[discovery] curl init failed for " << purpose << "\n";
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(period_ms_));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[discovery] curl error on " << purpose << ": " << res << "\n";
        return false;
    }
    if (code < 200 || code >= 300) {
        std::cerr << "[discovery] http " << code << " on " << purpose << "\n";
        return false;
    }
    return true;
}

bool DiscoveryClient::fetchPeers() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[discovery] curl init failed for peers" << std::endl;
        return false;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        auto* out = static_cast<std::string*>(userdata);
        out->append(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(period_ms_));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[discovery] curl error on peers: " << res << std::endl;
        return false;
    }
    if (code < 200 || code >= 300) {
        std::cerr << "[discovery] http " << code << " on peers" << std::endl;
        return false;
    }

    // 期望返回 JSON 对象数组：[{"endpoint":"...","role":"...","zone":"...","qos":"reliable"}, ...]
    std::vector<PeerInfo> parsed;
    std::string current_obj;
    bool in_obj = false;
    for (char c : response) {
        if (c == '{') {
            in_obj = true;
            current_obj.clear();
            current_obj.push_back(c);
        } else if (c == '}' && in_obj) {
            current_obj.push_back(c);
            in_obj = false;
            auto extract = [&](const std::string& key) {
                std::string pat = "\"" + key + "\":\"";
                size_t pos = current_obj.find(pat);
                if (pos == std::string::npos) return std::string();
                pos += pat.size();
                size_t end = current_obj.find('"', pos);
                if (end == std::string::npos) return std::string();
                return current_obj.substr(pos, end - pos);
            };
            PeerInfo info;
            info.endpoint = extract("endpoint");
            info.role = extract("role");
            info.zone = extract("zone");
            info.qos = extract("qos");
            if (!info.endpoint.empty()) {
                parsed.push_back(std::move(info));
            }
        } else if (in_obj) {
            current_obj.push_back(c);
        }
    }

    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        peer_infos_.swap(parsed);
    }
    return true;
}

std::vector<std::string> DiscoveryClient::getPeers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::vector<std::string> endpoints;
    for (const auto& p : peer_infos_) {
        if (!p.role.empty() && !node_role_.empty() && p.role != node_role_) {
            continue;
        }
        if (!node_zone_.empty() && !p.zone.empty() && p.zone != node_zone_) {
            continue;
        }
        if (!p.qos.empty() && p.qos != "reliable" && p.qos != "best_effort") {
            continue;
        }
        endpoints.push_back(p.endpoint);
    }
    return endpoints;
}

std::vector<PeerInfo> DiscoveryClient::getPeerInfos() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    return peer_infos_;
}

void DiscoveryClient::refreshPeers() {
    (void)fetchPeers();
}
