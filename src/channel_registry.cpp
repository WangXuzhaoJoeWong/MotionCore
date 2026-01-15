#include "internal/channel_registry.h"
#include "logger.h"

#include "observability.h"

#include <sstream>

namespace channel_registry {

ChannelRegistry& ChannelRegistry::instance() {
    static ChannelRegistry reg;
    return reg;
}

void ChannelRegistry::set_fastdds(std::map<std::string, std::shared_ptr<wxz::core::FastddsChannel>> channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    fastdds_channels_ = std::move(channels);

    // Minimal registry-level gauges for ops.
    wxz::core::metrics().gauge_set("wxz_channel_registry_fastdds_channels", static_cast<double>(fastdds_channels_.size()), {});
}

void ChannelRegistry::set_shm(std::map<std::string, std::shared_ptr<wxz::core::ShmChannel>> channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    shm_ = std::move(channels);

    // Minimal registry-level gauges for ops.
    wxz::core::metrics().gauge_set("wxz_channel_registry_shm_channels", static_cast<double>(shm_.size()), {});
}

std::shared_ptr<wxz::core::FastddsChannel> ChannelRegistry::fastdds(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fastdds_channels_.find(name);
    if (it == fastdds_channels_.end()) return nullptr;
    return it->second;
}

std::shared_ptr<wxz::core::ShmChannel> ChannelRegistry::shm(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = shm_.find(name);
    if (it == shm_.end()) return nullptr;
    return it->second;
}

std::vector<std::string> ChannelRegistry::list_fastdds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(fastdds_channels_.size());
    for (const auto& kv : fastdds_channels_) names.push_back(kv.first);
    return names;
}

std::vector<std::string> ChannelRegistry::list_shm() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(shm_.size());
    for (const auto& kv : shm_) names.push_back(kv.first);
    return names;
}

void ChannelRegistry::log_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    wxz::core::Logger::getInstance().info(to_json(false));
}

    std::string ChannelRegistry::to_json(bool group_by_module) const {
        std::ostringstream oss;
        oss << "{";
        // FastDDS metrics
        oss << "\"fastdds\":[";
        bool first = true;
        for (const auto& kv : fastdds_channels_) {
            if (!first) oss << ","; first = false;
            const auto& name = kv.first;
            const auto& ch = kv.second;
            oss << "{\"channel\":\"" << name << "\","
                "\"messages_received\":" << ch->messages_received() << ","
                "\"recv_drop_pool_exhausted\":" << ch->recv_drop_pool_exhausted() << "," 
                "\"recv_drop_dispatch_rejected\":" << ch->recv_drop_dispatch_rejected() << "," 
                "\"last_publish_duration_ns\":" << ch->last_publish_duration_ns() << "}";
        }
        oss << "],";
        // Inproc metrics
        oss << "\"inproc\":[";
        first = true;
        for (const auto& kv : inproc_) {
            if (!first) oss << ","; first = false;
            auto& ch = kv.second;
            oss << "{\"channel\":\"" << kv.first << "\","
                "\"publish_success\":" << ch->publish_success() << ","
                "\"publish_fail\":" << ch->publish_fail() << ","
                "\"messages_delivered\":" << ch->messages_delivered() << "}";
        }
        oss << "],";
        // SHM metrics
        oss << "\"shm\":[";
        first = true;
        for (const auto& kv : shm_) {
            if (!first) oss << ","; first = false;
            auto& ch = kv.second;
            oss << "{\"channel\":\"" << kv.first << "\","
                "\"publish_success\":" << ch->publish_success() << ","
                "\"publish_fail\":" << ch->publish_fail() << ","
                "\"messages_delivered\":" << ch->messages_delivered() << "}";
        }
        oss << "]";
        oss << "}";
        return oss.str();
    }

void ChannelRegistry::stop_all() {
    std::vector<std::shared_ptr<wxz::core::FastddsChannel>> fastdds;
    std::vector<std::shared_ptr<wxz::core::InprocChannel>> inproc;
    std::vector<std::shared_ptr<wxz::core::ShmChannel>> shm;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fastdds.reserve(fastdds_channels_.size());
        for (const auto& kv : fastdds_channels_) fastdds.push_back(kv.second);
        inproc.reserve(inproc_.size());
        for (const auto& kv : inproc_) inproc.push_back(kv.second);
        shm.reserve(shm_.size());
        for (const auto& kv : shm_) shm.push_back(kv.second);
    }

    for (auto& ch : fastdds) {
        if (ch) ch->stop();
    }
    for (auto& ch : inproc) {
        if (ch) ch->stop();
    }
    for (auto& ch : shm) {
        if (ch) ch->stop();
    }
}

void ChannelRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    fastdds_channels_.clear();
    inproc_.clear();
    shm_.clear();
}

void ChannelRegistry::unsubscribe_owner(void* owner) {
    if (!owner) return;
    std::vector<std::shared_ptr<wxz::core::FastddsChannel>> fastdds;
    std::vector<std::shared_ptr<wxz::core::InprocChannel>> inproc;
    std::vector<std::shared_ptr<wxz::core::ShmChannel>> shm;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fastdds.reserve(fastdds_channels_.size());
        for (const auto& kv : fastdds_channels_) fastdds.push_back(kv.second);
        inproc.reserve(inproc_.size());
        for (const auto& kv : inproc_) inproc.push_back(kv.second);
        shm.reserve(shm_.size());
        for (const auto& kv : shm_) shm.push_back(kv.second);
    }

    for (auto& ch : fastdds) {
        if (ch) ch->unsubscribe_owner(owner);
    }
    for (auto& ch : inproc) {
        if (ch) ch->unsubscribe_owner(owner);
    }
    for (auto& ch : shm) {
        if (ch) ch->unsubscribe_owner(owner);
    }
}

} // namespace channel_registry
