#include "dto/event_dto_cdr.h"

#include "dto/dto_core.h"

namespace wxz::dto {

bool encode_event_dto_cdr(const ::EventDTO& dto,
                          std::vector<std::uint8_t>& out,
                          std::size_t initial_reserve) {
    CdrSerializer ser(out, initial_reserve);

    // 字段顺序必须与 dto/EventDTO.idl 一致。
    if (!ser.write_uint32(dto.version)) return false;
    if (!ser.write_string(dto.schema_id)) return false;
    if (!ser.write_string(dto.topic)) return false;
    if (!ser.write_string(dto.payload)) return false;
    if (!ser.write_uint64(dto.timestamp)) return false;
    if (!ser.write_string(dto.event_id)) return false;
    if (!ser.write_string(dto.source)) return false;
    return true;
}

bool decode_event_dto_cdr(const std::vector<std::uint8_t>& buf, ::EventDTO& out) {
    CdrDeserializer de(buf);

    // 字段顺序必须与 dto/EventDTO.idl 一致。
    if (!de.read_uint32(out.version)) return false;
    if (!de.read_string(out.schema_id)) return false;
    if (!de.read_string(out.topic)) return false;
    if (!de.read_string(out.payload)) return false;
    if (!de.read_uint64(out.timestamp)) return false;
    if (!de.read_string(out.event_id)) return false;
    if (!de.read_string(out.source)) return false;
    return true;
}

bool decode_event_dto_cdr(const std::uint8_t* data, std::size_t size, ::EventDTO& out) {
    CdrDeserializer de(data, size);

    // 字段顺序必须与 dto/EventDTO.idl 一致。
    if (!de.read_uint32(out.version)) return false;
    if (!de.read_string(out.schema_id)) return false;
    if (!de.read_string(out.topic)) return false;
    if (!de.read_string(out.payload)) return false;
    if (!de.read_uint64(out.timestamp)) return false;
    if (!de.read_string(out.event_id)) return false;
    if (!de.read_string(out.source)) return false;
    return true;
}

} // namespace wxz::dto
