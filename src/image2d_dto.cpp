#include "dto/image2d_dto.h"

namespace wxz::dto {

const TypeInfo Image2dDto::kType{
    "sensor.image2d", // name
    1,                 // version
    "cdr",            // content_type
    0,                 // schema_hash (可选填充)
    "ignore_unknown"  // compat
};

bool Image2dDto::serialize(Serializer &out) const {
    return out.write_uint32(width) && out.write_uint32(height) && out.write_uint32(step) &&
           out.write_string(encoding) && out.write_string(frame_id) && out.write_string(data);
}

bool Image2dDto::deserialize(Deserializer &in) {
    return in.read_uint32(width) && in.read_uint32(height) && in.read_uint32(step) &&
           in.read_string(encoding) && in.read_string(frame_id) && in.read_string(data);
}

bool register_image2d_dto() {
    return TypeRegistry::instance().registerFactory(Image2dDto::kType, [] {
        return std::make_unique<Image2dDto>();
    });
}

static bool kRegImage2d = register_image2d_dto();

} // namespace wxz::dto
