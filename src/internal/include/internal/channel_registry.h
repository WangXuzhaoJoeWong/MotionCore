#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fastdds_channel.h"
#include "inproc_channel.h"
#include "shm_channel.h"

namespace channel_registry {

class ChannelRegistry {
public:
    static ChannelRegistry& instance();

    void set_fastdds(std::map<std::string, std::shared_ptr<wxz::core::FastddsChannel>> channels);
    std::shared_ptr<wxz::core::FastddsChannel> fastdds(const std::string& name);

    std::vector<std::string> list_fastdds() const;

    void set_shm(std::map<std::string, std::shared_ptr<wxz::core::ShmChannel>> channels);
    std::shared_ptr<wxz::core::ShmChannel> shm(const std::string& name);
    std::vector<std::string> list_shm() const;

    // Diagnostics: log current counters for all registered channels.
    void log_metrics() const;
    // Structured metrics: JSON string grouped by transport/module.
    std::string to_json(bool group_by_module) const;

    // Lifecycle helpers: explicitly stop background threads and drop references.
    // Safe to call multiple times.
    void stop_all();
    void clear();

    // Subscription lifecycle: bulk-unsubscribe by owner tag (e.g. plugin instance pointer).
    // This is designed to be called before dlclose(plugin_so) to ensure no std::function
    // created in the plugin remains alive in core.
    void unsubscribe_owner(void* owner);

private:
    ChannelRegistry() = default;
    ChannelRegistry(const ChannelRegistry&) = delete;
    ChannelRegistry& operator=(const ChannelRegistry&) = delete;

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<wxz::core::FastddsChannel>> fastdds_channels_;
    std::map<std::string, std::shared_ptr<wxz::core::InprocChannel>> inproc_;
    std::map<std::string, std::shared_ptr<wxz::core::ShmChannel>> shm_;
};

} // namespace channel_registry
