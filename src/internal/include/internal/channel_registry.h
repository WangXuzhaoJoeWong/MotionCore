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

    // 诊断：打印所有已注册通道的当前计数器。
    void log_metrics() const;
    // 结构化指标：按 transport/module 分组输出 JSON 字符串。
    std::string to_json(bool group_by_module) const;

    // 生命周期辅助：显式停止后台线程并释放引用。
    // 可重复调用（幂等）。
    void stop_all();
    void clear();

    // 订阅生命周期：按 owner tag 批量取消订阅（例如插件实例指针）。
    // 设计目标：在 dlclose(plugin_so) 之前调用，确保 core 中不会残留由插件创建的 std::function。
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
