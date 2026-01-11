#include "dto/simple_pose_dto.h"

namespace wxz::dto {

const TypeInfo SimplePoseDto::kType{
    "wxz.dto.simplepose",
    1,
    "cdr",
    0,
    "ignore_unknown"
};

bool SimplePoseDto::serialize(Serializer &out) const {
    return out.write_double(x) && out.write_double(y) && out.write_double(z) &&
           out.write_double(yaw);
}

bool SimplePoseDto::deserialize(Deserializer &in) {
    return in.read_double(x) && in.read_double(y) && in.read_double(z) &&
           in.read_double(yaw);
}

bool register_simple_pose_dto() {
    return TypeRegistry::instance().registerFactory(SimplePoseDto::kType, [] {
        return std::make_unique<SimplePoseDto>();
    });
}

static bool kRegSimplePose = register_simple_pose_dto();

} // namespace wxz::dto
