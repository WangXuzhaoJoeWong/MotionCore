#pragma once

#include <cstdint>
#include <string>

#include "dto/event_dto.h"
#include "service_common.h"

namespace wxz::core {

// `fault/status` 的 KV 契约。
// 最小字段：
// - kind=fault
// - service,fault,active,severity,err_code,err,api_version,schema_version,domain,ts_ms
// 可选字段：
// - version
struct FaultStatus {
    std::string service;   // 上报方服务名（例如 wxz_arm_control_service）
    std::string fault;     // 稳定 fault id（例如 arm.sdk）

    bool active{false};
    std::string severity{"error"}; // info|warn|error|fatal

    int err_code{0};
    std::string err;

    // 治理
    std::string version; // 自由文本，可选
    int api_version{1};
    int schema_version{1};

    int domain{0};
};

inline EventDTOUtil::KvMap build_fault_status_kv(const FaultStatus& st) {
    EventDTOUtil::KvMap kv;
    kv["kind"] = "fault";
    kv["service"] = st.service;
    kv["fault"] = st.fault;
    kv["active"] = st.active ? "1" : "0";
    kv["severity"] = st.severity;
    kv["err_code"] = std::to_string(st.err_code);
    kv["err"] = st.err;
    if (!st.version.empty()) kv["version"] = st.version;
    kv["api_version"] = std::to_string(st.api_version);
    kv["schema_version"] = std::to_string(st.schema_version);
    kv["domain"] = std::to_string(st.domain);
    kv["ts_ms"] = std::to_string(now_epoch_ms());
    return kv;
}

inline std::string build_fault_status_payload(const FaultStatus& st) {
    return EventDTOUtil::buildPayloadKv(build_fault_status_kv(st));
}

} // namespace wxz::core
