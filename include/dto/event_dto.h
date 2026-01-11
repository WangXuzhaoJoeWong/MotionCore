#ifndef EVENT_DTO_H
#define EVENT_DTO_H

#include <string>
#include <cstdint>
#include <unordered_map>

// 传输边界的稳定数据契约（与中间件解耦）
// 对应 IDL: dto/EventDTO.idl
struct EventDTO {
    // 协议版本号，用于破坏性变更时升级（例如 1 -> 2）
    uint32_t version{1};

    // 业务事件 schema，例如 "ws.detection.v1"
    std::string schema_id{"event.v1"};

    // 逻辑 topic 名（用于路由和归类）
    std::string topic;

    // 业务负载，推荐格式："key=value;key2=value2" 或 JSON
    std::string payload;

    // 事件发生时间戳（Unix epoch 毫秒），用于审计与链路追踪
    std::uint64_t timestamp{0};

    // 事件唯一标识，用于去重与追踪；可以是 UUID 或 "ts-random" 形式
    std::string event_id;

    // 事件来源标识，例如 "rw_luggage_workstation"、"sensor_gateway"
    std::string source;
};

// 当 EventDTO 的 payload 使用 "key=value;key2=value2" 形式时的辅助解析/构造工具
struct EventDTOUtil {
    using KvMap = std::unordered_map<std::string, std::string>;

    // 将 payload 解析为 {key -> value}，忽略空项与无 '=' 的片段
    static KvMap parsePayloadKv(const std::string& payload);

    // 将 {key -> value} 组装为 "k=v;..." 字符串；不做转义，假定 key/value 不包含 ';' 和 '='
    static std::string buildPayloadKv(const KvMap& kvs);

    // 填充通用元数据：
    // - 若 timestamp 为 0，则填当前时间（毫秒）
    // - 若 event_id 为空，则生成 "ts-rand" 形式的简单 ID
    // - 若 source 为空且提供了 default_source，则填充之
    static void fillMeta(EventDTO& dto, const std::string& default_source = {});
};

#endif // EVENT_DTO_H
