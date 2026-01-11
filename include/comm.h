#pragma once

// 注意（legacy）：
// 本头文件定义了历史遗留的 ICommEndpoint/CommFactory 抽象。
// 平台推荐的通信抽象为：
//   - 跨进程/跨机器：wxz::core::FastddsChannel
//   - 进程内：wxz::core::InprocChannel
//   - 同机 IPC（大 payload）：wxz::core::ShmChannel
//
// 下游/业务侧加固开关：
// - 定义 WXZ_FORBID_LEGACY_COMM_ENDPOINT=1：包含本头文件将触发编译期错误。
// - 定义 WXZ_DEPRECATE_LEGACY_COMM_ENDPOINT=1：用 [[deprecated]] 标注 legacy 类型并产生告警。

#if defined(WXZ_FORBID_LEGACY_COMM_ENDPOINT)
#error "comm.h is legacy. Use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. See docs/ref/推荐用法-P0-通信抽象.md"
#endif

#if defined(WXZ_DEPRECATE_LEGACY_COMM_ENDPOINT)
    #if defined(__has_cpp_attribute)
        #if __has_cpp_attribute(deprecated)
            #define WXZ_LEGACY_COMM_ENDPOINT_DEPRECATED(msg) [[deprecated(msg)]]
        #else
            #define WXZ_LEGACY_COMM_ENDPOINT_DEPRECATED(msg)
        #endif
    #else
        #define WXZ_LEGACY_COMM_ENDPOINT_DEPRECATED(msg)
    #endif
#else
    #define WXZ_LEGACY_COMM_ENDPOINT_DEPRECATED(msg)
#endif

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wxz::core {

struct MessageHeader {
    std::string id;
    std::string source;
    std::string type;
    std::string qos; // 例如 reliable/best-effort
    std::uint64_t timestamp{0};
};

struct Message {
    MessageHeader header;
    std::vector<std::uint8_t> body;
};

class WXZ_LEGACY_COMM_ENDPOINT_DEPRECATED(
    "ICommEndpoint is legacy. Prefer wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. See docs/ref/推荐用法-P0-通信抽象.md")
ICommEndpoint {
public:
    using MessageHandler = std::function<void(const Message&)>;
    virtual ~ICommEndpoint() = default;

    virtual bool publish(const std::string& topic, const Message& msg) = 0;
    virtual bool subscribe(const std::string& topic, MessageHandler handler) = 0;
    virtual bool request(const std::string& topic, const Message& req, Message& resp) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
};

struct CommConfig {
    std::string type;      // "zmq" | "fastdds" | 其他
    std::string endpoint;  // bind/connect URI 或 DDS domain 信息
    std::string peer_hint; // 可选 discovery hint
};

class WXZ_LEGACY_COMM_ENDPOINT_DEPRECATED(
    "CommFactory is legacy. Prefer constructing wxz::core::*Channel directly (or via config-driven channel_factory). See docs/ref/推荐用法-P0-通信抽象.md")
CommFactory {
public:
    static std::unique_ptr<ICommEndpoint> create(const CommConfig& cfg);
};

} // namespace wxz::core
