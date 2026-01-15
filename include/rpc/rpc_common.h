#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "service_common.h" // ChannelQoS + default_reliable_qos

namespace wxz::core::rpc {

enum class RpcErrorCode : int {
    Ok = 0,
    Timeout = 1,
    TransportError = 2,
    ParseError = 3,
    RemoteError = 4,
    NotStarted = 5,
    Cancelled = 6,
};

inline std::string_view to_string(RpcErrorCode c) {
    switch (c) {
    case RpcErrorCode::Ok: return "ok";
    case RpcErrorCode::Timeout: return "timeout";
    case RpcErrorCode::TransportError: return "transport_error";
    case RpcErrorCode::ParseError: return "parse_error";
    case RpcErrorCode::RemoteError: return "remote_error";
    case RpcErrorCode::NotStarted: return "not_started";
    case RpcErrorCode::Cancelled: return "cancelled";
    default: return "unknown";
    }
}

struct RpcClientOptions {
    int domain{0};
    std::string request_topic;
    std::string reply_topic;

    // QoS：默认可靠 + 常用参数（与 default_reliable_qos() 一致）。
    ChannelQoS qos = default_reliable_qos();

    // 用于生成请求 id 的前缀（便于跨进程排障）。
    std::string client_id_prefix{""};

    // 可观测性标签：建议填 service 名称或模块名。
    std::string metrics_scope{""};
};

struct RpcServerOptions {
    int domain{0};
    std::string request_topic;
    std::string reply_topic;

    // QoS：默认可靠 + 常用参数（与 default_reliable_qos() 一致）。
    ChannelQoS qos = default_reliable_qos();

    // 用于 metrics label 区分服务实例。
    std::string service_name{""};

    // 可观测性标签：建议填 service 名称或模块名。
    std::string metrics_scope{""};
};

} // namespace wxz::core::rpc
