#pragma once

#include <string>

namespace eprosima::fastdds::dds {
class DomainParticipant;
}

namespace wxz::core::internal {

// 加载 FastDDS XML profiles（每个进程只加载一次）。
// 优先级：
// 1) 环境变量覆盖：WXZ_FASTDDS_PROFILES_FILE（若设置）。
// 2) 否则，尝试加载安装树中的默认 profiles：
//      <prefix>/share/wxz_robot/resources/fastdds_profiles_release.xml
//    其中 <prefix> 通过 /proc/self/exe 推导。
//
// 失败行为：
// - 若设置了 WXZ_FASTDDS_PROFILES_FILE 但文件不可读/不可加载：抛出 std::runtime_error。
// - 若默认 profiles 缺失/不可读：尽力而为（no-op）。
void load_fastdds_profiles_from_env_once();

// 使用与 wxz::core::FastddsChannel 相同的“环境变量驱动行为”来创建 DomainParticipant：
// - 若设置 WXZ_FASTDDS_PARTICIPANT_PROFILE：调用 create_participant_with_profile(domain_id, profile)，
//   若 profile 不存在则抛异常。
// - 否则：
//   - 若已加载安装树默认 profiles：create_participant_with_profile(domain_id, wxz_release_participant)
//   - 否则：create_participant(domain_id, PARTICIPANT_QOS_DEFAULT)，以便应用 XML 默认 profile。
//
// 备注：
// - 该函数不会做 DDS-Security 预检查；调用方需自行处理。
// - 该函数会触发 XML profiles 加载（每进程一次）。
// - factory 失败时返回 nullptr；仅在明确的契约违规时抛异常（例如 profile 名无效、profiles 文件无效）。
[[nodiscard]] eprosima::fastdds::dds::DomainParticipant* create_fastdds_participant_from_env(int domain_id);

} // namespace wxz::core::internal
