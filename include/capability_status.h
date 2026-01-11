#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dto/event_dto.h"
#include "service_common.h"

namespace wxz::core {

struct CapabilityStatus {
    // 身份
    std::string service;   // 例如 wxz_bt_service
    std::string type;      // 例如 bt/orchestrator/device/planner
    std::string version;   // 自由文本，可选

    // 治理
    int api_version{1};
    int schema_version{1};

    // 运行时
    int domain{0};
    bool ok{true};

    // 本服务使用的 topics（可选，尽力上报）
    std::vector<std::string> topics_pub;
    std::vector<std::string> topics_sub;
};

inline std::string join_csv(const std::vector<std::string>& xs) {
    std::string out;
    for (size_t i = 0; i < xs.size(); ++i) {
        if (i) out.push_back(',');
        out += xs[i];
    }
    return out;
}

// `capability/status` 的 KV 契约。
// 最小字段：
// - kind=capability
// - service,type,api_version,schema_version,domain,ok,ts_ms
// 可选字段：
// - version, topics_pub, topics_sub
inline EventDTOUtil::KvMap build_capability_kv(const CapabilityStatus& st) {
    EventDTOUtil::KvMap kv;
    kv["kind"] = "capability";
    kv["service"] = st.service;
    kv["type"] = st.type;
    if (!st.version.empty()) kv["version"] = st.version;
    kv["api_version"] = std::to_string(st.api_version);
    kv["schema_version"] = std::to_string(st.schema_version);
    kv["domain"] = std::to_string(st.domain);
    kv["ok"] = st.ok ? "1" : "0";
    kv["ts_ms"] = std::to_string(now_epoch_ms());
    if (!st.topics_pub.empty()) kv["topics_pub"] = join_csv(st.topics_pub);
    if (!st.topics_sub.empty()) kv["topics_sub"] = join_csv(st.topics_sub);
    return kv;
}

inline std::string build_capability_payload(const CapabilityStatus& st) {
    return EventDTOUtil::buildPayloadKv(build_capability_kv(st));
}

} // namespace wxz::core
