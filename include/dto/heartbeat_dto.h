#pragma once

#include <cstdint>
#include <string>

// 对应 IDL: dto/HeartbeatDTO.idl
struct HeartbeatDTO {
    // 协议版本号（破坏性变更时升级）
    std::uint32_t version{1};

    // 节点/进程标识，例如 "node_container" / "workstation"
    std::string node;

    // 上报时间戳（Unix epoch 毫秒）
    std::uint64_t timestamp{0};

    // 简单状态码：0=UNKNOWN, 1=HEALTHY, 2=DEGRADED, 3=UNHEALTHY
    std::uint32_t state{0};

    // 可选文本信息（用于诊断/人类可读）
    std::string message;
};
