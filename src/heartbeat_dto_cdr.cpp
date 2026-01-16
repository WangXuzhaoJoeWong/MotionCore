#include "dto/heartbeat_dto_cdr.h"

#include "dto/dto_core.h"

namespace wxz::dto {

bool encode_heartbeat_dto_cdr(const ::HeartbeatDTO& dto,
                             std::vector<std::uint8_t>& out,
                             std::size_t initial_reserve) {
    CdrSerializer ser(out, initial_reserve);

    // 字段顺序必须与 dto/HeartbeatDTO.idl 一致。
    if (!ser.write_uint32(dto.version)) return false;
    if (!ser.write_string(dto.node)) return false;
    if (!ser.write_uint64(dto.timestamp)) return false;
    if (!ser.write_uint32(dto.state)) return false;
    if (!ser.write_string(dto.message)) return false;
    return true;
}

bool decode_heartbeat_dto_cdr(const std::vector<std::uint8_t>& buf, ::HeartbeatDTO& out) {
    CdrDeserializer de(buf);

    // 字段顺序必须与 dto/HeartbeatDTO.idl 一致。
    if (!de.read_uint32(out.version)) return false;
    if (!de.read_string(out.node)) return false;
    if (!de.read_uint64(out.timestamp)) return false;
    if (!de.read_uint32(out.state)) return false;
    if (!de.read_string(out.message)) return false;
    return true;
}

} // namespace wxz::dto
