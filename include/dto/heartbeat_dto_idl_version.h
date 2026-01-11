#pragma once

namespace wxz::dto {

// 基于 IDL 的 HeartbeatDTO 版本契约。
//
// 策略（P2）：当 dto/HeartbeatDTO.idl 发生“影响兼容性”的修改时，
// 需要更新该哈希以匹配新的 IDL 内容。
//
// 回归入口：ctest -R TestDtoIdlVersion -V
inline constexpr const char* kHeartbeatDtoIdlSha256 =
    "9caf9790360ba32b9804f1f9b83177c058dffead9d036a74e9896d4ccba9c7b6";

}  // namespace wxz::dto
