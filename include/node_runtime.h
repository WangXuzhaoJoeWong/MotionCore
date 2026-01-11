#pragma once

#include <memory>
#include <string>

namespace wxz::core {

struct RuntimeConfig {
    std::string node_name;
    int threads{1};           // 1 = 单线程
    bool service_mode{false}; // false = 进程内；true = 独立服务进程
};

class INodeRuntime {
public:
    virtual ~INodeRuntime() = default;
    virtual bool configure(const RuntimeConfig& cfg) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
};

} // namespace wxz::core
