#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "dto/heartbeat_dto.h"

namespace wxz::dto {

// 使用与 dto/HeartbeatDTO.idl 匹配的 Fast CDR 规则，对 HeartbeatDTO 进行编码/解码。
// 传输层可使用 wxz::core::FastddsChannel 发送原始字节。

bool encode_heartbeat_dto_cdr(const ::HeartbeatDTO& dto,
                             std::vector<std::uint8_t>& out,
                             std::size_t initial_reserve = 1024);

bool decode_heartbeat_dto_cdr(const std::vector<std::uint8_t>& buf, ::HeartbeatDTO& out);

} // namespace wxz::dto
