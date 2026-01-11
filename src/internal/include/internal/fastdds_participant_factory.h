#pragma once

#include <string>

namespace eprosima::fastdds::dds {
class DomainParticipant;
}

namespace wxz::core::internal {

// Load FastDDS XML profiles (once per process).
// Priority:
// 1) Env override WXZ_FASTDDS_PROFILES_FILE (if set).
// 2) Otherwise, try install-tree default profiles under:
//      <prefix>/share/wxz_robot/resources/fastdds_profiles_release.xml
//    where <prefix> is inferred from /proc/self/exe.
//
// Failure behavior:
// - If WXZ_FASTDDS_PROFILES_FILE is set but unreadable/unloadable: throws std::runtime_error.
// - If default profiles are missing/unreadable: best-effort (no-op).
void load_fastdds_profiles_from_env_once();

// Create a DomainParticipant using the same env-controlled behavior as wxz::core::FastddsChannel:
// - If WXZ_FASTDDS_PARTICIPANT_PROFILE is set: create_participant_with_profile(domain_id, profile)
//   and throw if profile does not exist.
// - Else:
//   - If install-tree default profiles were loaded: create_participant_with_profile(domain_id, wxz_release_participant)
//   - Otherwise: create_participant(domain_id, PARTICIPANT_QOS_DEFAULT) so XML default profile can apply.
//
// Notes:
// - This does NOT perform DDS-Security precheck; callers should do that separately.
// - This function performs XML profiles loading (once per process).
// - Returns nullptr on factory failure; throws only on explicit contract violations (e.g. bad profile, bad profiles file).
[[nodiscard]] eprosima::fastdds::dds::DomainParticipant* create_fastdds_participant_from_env(int domain_id);

} // namespace wxz::core::internal
