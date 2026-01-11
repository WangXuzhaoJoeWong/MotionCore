#pragma once

namespace wxz::dto {

// 基于 IDL 的 EventDTO 版本契约。
//
// 策略（P2）：当 dto/EventDTO.idl 发生“影响兼容性”的修改时，
// 需要更新该哈希以匹配新的 IDL 内容。
//
// 回归入口：ctest -R TestDtoIdlVersion -V
inline constexpr const char* kEventDtoIdlSha256 =
    "c72f4bf4025c3c34a3f3d58f1c90afba33c5bd5cb0fb0e67b17c72272da65603";

}  // namespace wxz::dto
