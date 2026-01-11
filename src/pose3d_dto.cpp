#include "dto/pose3d_dto.h"

namespace wxz::dto {

const TypeInfo Pose3dDto::kType{
    "geometry.pose3d",
    1,
    "cdr",
    0,
    "ignore_unknown"
};

bool Pose3dDto::serialize(Serializer &out) const {
    return out.write_double(x) && out.write_double(y) && out.write_double(z) &&
           out.write_double(qx) && out.write_double(qy) && out.write_double(qz) &&
           out.write_double(qw) && out.write_string(frame_id);
}

bool Pose3dDto::deserialize(Deserializer &in) {
    return in.read_double(x) && in.read_double(y) && in.read_double(z) &&
           in.read_double(qx) && in.read_double(qy) && in.read_double(qz) &&
           in.read_double(qw) && in.read_string(frame_id);
}

bool register_pose3d_dto() {
    return TypeRegistry::instance().registerFactory(Pose3dDto::kType, [] {
        return std::make_unique<Pose3dDto>();
    });
}

static bool kRegPose3d = register_pose3d_dto();

} // namespace wxz::dto
