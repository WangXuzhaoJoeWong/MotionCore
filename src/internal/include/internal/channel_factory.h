#pragma once

#include <map>
#include <memory>
#include <string>

#include "internal/config.h"
#include "fastdds_channel.h"
#include "shm_channel.h"

// 从配置构建 channels 的工厂辅助函数。
namespace channel_factory {

std::map<std::string, std::shared_ptr<wxz::core::FastddsChannel>>
build_fastdds_channels_from_config(const Config& cfg);

std::map<std::string, std::shared_ptr<wxz::core::ShmChannel>>
build_shm_channels_from_config(const Config& cfg, bool create);

} // namespace channel_factory
