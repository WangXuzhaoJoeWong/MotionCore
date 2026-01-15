#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "dto/event_dto.h"

namespace wxz::dto {

// 使用与 dto/EventDTO.idl 匹配的 Fast CDR 规则，对 EventDTO 进行编码/解码。
// 传输层可使用 wxz::core::FastddsChannel 发送原始字节。

bool encode_event_dto_cdr(const ::EventDTO& dto,
                          std::vector<std::uint8_t>& out,
                          std::size_t initial_reserve = 8 * 1024);

bool decode_event_dto_cdr(const std::vector<std::uint8_t>& buf, ::EventDTO& out);

bool decode_event_dto_cdr(const std::uint8_t* data, std::size_t size, ::EventDTO& out);

} // namespace wxz::dto
