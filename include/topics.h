#pragma once

namespace wxz::core {

// 默认 topic 命名（字面量的唯一口径）。
inline constexpr const char* kCapabilityStatusTopic = "capability/status";
inline constexpr const char* kFaultStatusTopic = "fault/status";
inline constexpr const char* kHeartbeatStatusTopic = "heartbeat/status";

// 环境变量开关（调用方可不改代码覆盖 topic 名）。
inline constexpr const char* kEnvCapabilityStatusTopic = "WXZ_CAPABILITY_STATUS_TOPIC";
inline constexpr const char* kEnvFaultStatusTopic = "WXZ_FAULT_STATUS_TOPIC";
inline constexpr const char* kEnvHeartbeatStatusTopic = "WXZ_HEARTBEAT_STATUS_TOPIC";

} // namespace wxz::core
