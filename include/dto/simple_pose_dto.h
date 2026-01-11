#pragma once

#include "dto/dto_core.h"
#include <string>

namespace wxz::dto {

// 简化位姿 DTO：位置 + 航向（yaw）
class SimplePoseDto : public IDto {
public:
    SimplePoseDto() = default;

    const TypeInfo &type() const override { return kType; }
    uint32_t version() const override { return kType.version; }

    bool serialize(Serializer &out) const override;
    bool deserialize(Deserializer &in) override;
    std::unique_ptr<IDto> clone() const override { return std::make_unique<SimplePoseDto>(*this); }

    double x{0.0};
    double y{0.0};
    double z{0.0};
    double yaw{0.0};

    static const TypeInfo kType;
};

bool register_simple_pose_dto();

} // namespace wxz::dto
