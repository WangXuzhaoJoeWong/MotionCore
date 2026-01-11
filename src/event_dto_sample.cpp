#include "dto/event_dto_sample.h"

namespace wxz::dto {

const TypeInfo EventDtoSample::kType{
    "sample.event", // name
    1,               // version
    "binary",       // content_type（示例；可替换为 "cdr"）
    0,               // schema_hash (可选)
    "ignore_unknown"};

bool EventDtoSample::serialize(Serializer &out) const {
    return out.write_string(id) && out.write_uint64(timestamp_ms) && out.write_string(source) && out.write_string(detail);
}

bool EventDtoSample::deserialize(Deserializer &in) {
    return in.read_string(id) && in.read_uint64(timestamp_ms) && in.read_string(source) && in.read_string(detail);
}

bool register_event_dto_sample() {
    return TypeRegistry::instance().registerFactory(EventDtoSample::kType, [] {
        return std::make_unique<EventDtoSample>();
    });
}

// 静态注册（可选）：确保编译进核心时即注册。
static bool kRegistered = register_event_dto_sample();

} // namespace wxz::dto
