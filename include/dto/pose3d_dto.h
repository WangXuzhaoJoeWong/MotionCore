#pragma once

#include "dto/dto_core.h"
#include <string>

namespace wxz::dto {

// 位姿 DTO（位置 + 四元数），演示 CDR 强类型序列化
class Pose3dDto : public IDto {
public:
    Pose3dDto() = default;

    const TypeInfo &type() const override { return kType; }
    uint32_t version() const override { return kType.version; }

    bool serialize(Serializer &out) const override;
    bool deserialize(Deserializer &in) override;
    std::unique_ptr<IDto> clone() const override { return std::make_unique<Pose3dDto>(*this); }

    double x{0.0};
    double y{0.0};
    double z{0.0};
    double qx{0.0};
    double qy{0.0};
    double qz{0.0};
    double qw{1.0};
    std::string frame_id{"map"};

    static const TypeInfo kType;
};

bool register_pose3d_dto();

} // namespace wxz::dto
