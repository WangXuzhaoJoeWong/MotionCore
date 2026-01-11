#pragma once

#include "dto/dto_core.h"
#include <string>

namespace wxz::dto {

// 示例 DTO：事件消息（演示用）
class EventDtoSample : public IDto {
public:
    EventDtoSample() = default;

    const TypeInfo &type() const override { return kType; }
    uint32_t version() const override { return kType.version; }

    bool serialize(Serializer &out) const override;
    bool deserialize(Deserializer &in) override;
    std::unique_ptr<IDto> clone() const override {
        return std::make_unique<EventDtoSample>(*this);
    }

    // 业务字段（演示）
    std::string id;
    uint64_t timestamp_ms{0};
    std::string source;
    std::string detail;

    static const TypeInfo kType;
};

// 注册函数，由插件或静态代码调用
bool register_event_dto_sample();

} // namespace wxz::dto
