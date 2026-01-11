#pragma once

#include "dto/dto_core.h"
#include <string>
#include <vector>

namespace wxz::dto {

// 传感器图像 DTO 示例（强类型，演示 CDR 序列化）
class Image2dDto : public IDto {
public:
    Image2dDto() = default;

    const TypeInfo &type() const override { return kType; }
    uint32_t version() const override { return kType.version; }

    bool serialize(Serializer &out) const override;
    bool deserialize(Deserializer &in) override;
    std::unique_ptr<IDto> clone() const override { return std::make_unique<Image2dDto>(*this); }

    // 字段
    uint32_t width{0};
    uint32_t height{0};
    uint32_t step{0};
    std::string encoding;      // 如 rgb8/bgr8/mono8
    std::string data;          // 图像数据（可视为 bytes）
    std::string frame_id{"map"};

    static const TypeInfo kType;
};

bool register_image2d_dto();

} // namespace wxz::dto
