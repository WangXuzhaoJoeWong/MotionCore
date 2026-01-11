#pragma once

#include <string>

#include "dto/event_dto.h"
#include "service_common.h"

namespace wxz::core {

// `fault/action` 的 KV 契约。
// 最小字段：
// - kind=fault_action
// - target,action,fault,request_id,api_version,schema_version,domain,ts_ms
// 可选字段：
// - version
struct FaultAction {
    std::string target;     // 目标服务名（例如 wxz_arm_control_service）
    std::string action;     // 例如 reset|clear|stop|home
    std::string fault;      // 要操作的 fault id（是否必需取决于 action）
    std::string request_id; // 调用方提供的关联 id（用于串联请求/响应）

    // 治理
    std::string version; // 自由文本，可选
    int api_version{1};
    int schema_version{1};

    int domain{0};
};

inline EventDTOUtil::KvMap build_fault_action_kv(const FaultAction& a) {
    EventDTOUtil::KvMap kv;
    kv["kind"] = "fault_action";
    kv["target"] = a.target;
    kv["action"] = a.action;
    if (!a.fault.empty()) kv["fault"] = a.fault;
    kv["request_id"] = a.request_id;
    if (!a.version.empty()) kv["version"] = a.version;
    kv["api_version"] = std::to_string(a.api_version);
    kv["schema_version"] = std::to_string(a.schema_version);
    kv["domain"] = std::to_string(a.domain);
    kv["ts_ms"] = std::to_string(now_epoch_ms());
    return kv;
}

inline std::string build_fault_action_payload(const FaultAction& a) {
    return EventDTOUtil::buildPayloadKv(build_fault_action_kv(a));
}

} // namespace wxz::core
