#pragma once

namespace wxz::core::internal {

struct DdsSecurityEnvInfo {
    bool security_enabled{false};
};

// 解析 FASTDDS_ENVIRONMENT_FILE，并在启用 DDS-Security 时强制 fail-fast 语义。
// - env_file 为空时，返回 {security_enabled=false}。
// - 若启用 DDS-Security（存在 dds.sec.* 配置），则校验引用的安全文件是否存在。
// - 配置错误时抛出 std::runtime_error，并给出清晰的错误信息。
DdsSecurityEnvInfo precheck_dds_security_from_fastdds_env_file(const char* env_file);

} // namespace wxz::core::internal
