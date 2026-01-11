#pragma once

namespace wxz::core::internal {

struct DdsSecurityEnvInfo {
    bool security_enabled{false};
};

// Parses FASTDDS_ENVIRONMENT_FILE and enforces fail-fast semantics when DDS-Security is enabled.
// - If env_file is null/empty, returns {security_enabled=false}.
// - If DDS-Security is enabled (dds.sec.* properties present), validates referenced security files exist.
// - Throws std::runtime_error with a clear message when misconfigured.
DdsSecurityEnvInfo precheck_dds_security_from_fastdds_env_file(const char* env_file);

} // namespace wxz::core::internal
