#include "internal/fastdds_participant_factory.h"

#include "logger.h"

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>

#include <fastdds/rtps/attributes/ServerAttributes.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#ifdef __unix__
#include <unistd.h>
#endif

namespace wxz::core::internal {

namespace {

constexpr const char* kDefaultProfilesRelPath = "share/MotionCore/resources/fastdds_profiles_release.xml";
constexpr const char* kLegacyProfilesRelPath = "share/wxz_robot/resources/fastdds_profiles_release.xml";
constexpr const char* kDefaultParticipantProfile = "wxz_release_participant";
constexpr const char* kStrictParticipantProfile = "wxz_release_participant_strict";

struct ProfilesLoadState {
    bool used_env_file{false};
    bool used_default_file{false};
};

ProfilesLoadState& profiles_state() {
    static ProfilesLoadState s;
    return s;
}

std::string dirname_of(const std::string& p) {
    const auto pos = p.find_last_of('/');
    if (pos == std::string::npos) return std::string();
    if (pos == 0) return std::string("/");
    return p.substr(0, pos);
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string try_infer_install_prefix_from_proc_self_exe() {
#ifdef __unix__
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    const std::string exe(buf);
    const std::string bin_dir = dirname_of(exe);
    if (bin_dir.empty()) return std::string();
    return dirname_of(bin_dir); // <prefix>/bin/<exe> -> <prefix>
#else
    return std::string();
#endif
}

std::string default_profiles_path_from_install_prefix() {
    const std::string prefix = try_infer_install_prefix_from_proc_self_exe();
    if (prefix.empty()) return std::string();
    // 优先使用新的目录布局，但仍兼容旧布局。
    const std::string primary = join_path(prefix, kDefaultProfilesRelPath);
    {
        std::ifstream ifs(primary);
        if (ifs.good()) return primary;
    }

    const std::string legacy = join_path(prefix, kLegacyProfilesRelPath);
    {
        std::ifstream ifs(legacy);
        if (ifs.good()) return legacy;
    }

    return std::string();
}

bool env_truthy(const char* key) {
    const char* v = std::getenv(key);
    if (!v || !*v) return false;
    return (std::string(v) == "1" || std::string(v) == "true" || std::string(v) == "TRUE" || std::string(v) == "yes" || std::string(v) == "YES");
}

void log_transport_cfg(int domain_id,
                       const char* phase,
                       const eprosima::fastdds::dds::DomainParticipantQos& qos,
                       bool disable_shm_env,
                       bool force_udp_env) {
    using wxz::core::Logger;
    const auto builtin = qos.transport().use_builtin_transports;
    const auto user_n = qos.transport().user_transports.size();
    Logger::getInstance().info(std::string("FastDDS participant transport") +
                               " phase=" + phase +
                               " domain=" + std::to_string(domain_id) +
                               " use_builtin_transports=" + (builtin ? "1" : "0") +
                               " user_transports=" + std::to_string(user_n) +
                               " env_disable_shm=" + (disable_shm_env ? "1" : "0") +
                               " env_force_udp_only=" + (force_udp_env ? "1" : "0"));
}

void apply_udp_only_transport(eprosima::fastdds::dds::DomainParticipantQos& qos) {
    using namespace eprosima::fastdds::rtps;
    qos.transport().use_builtin_transports = false;
    qos.transport().user_transports.clear();
    auto udp = std::make_shared<UDPv4TransportDescriptor>();
    qos.transport().user_transports.push_back(udp);
}

eprosima::fastdds::dds::DomainParticipant* create_participant_with_fallback(
    int domain_id,
    eprosima::fastdds::dds::DomainParticipantFactory* factory,
    eprosima::fastdds::dds::DomainParticipantQos qos) {
    using namespace eprosima::fastdds::dds;

    const bool disable_shm_env = env_truthy("WXZ_FASTDDS_DISABLE_SHM");
    const bool force_udp_env = env_truthy("WXZ_FASTDDS_FORCE_UDP_ONLY");
    const bool force_udp_only = disable_shm_env || force_udp_env;
    if (force_udp_only) {
        apply_udp_only_transport(qos);
    }

    log_transport_cfg(domain_id, force_udp_only ? "precreate_udp_only" : "precreate_default", qos, disable_shm_env, force_udp_env);

    try {
        return factory->create_participant(domain_id, qos, nullptr, StatusMask::none());
    } catch (const std::bad_alloc&) {
        // FastDDS 的 SharedMemTransport 在共享内存资源无法分配/打开时可能抛出 std::bad_alloc。
        // 这里提供一个“尽力而为”的回退：改用仅 UDP 的 transports。
        if (force_udp_only) throw;

        wxz::core::Logger::getInstance().warn(
            std::string("FastDDS participant create threw std::bad_alloc; retrying with UDP-only transports") +
            " domain=" + std::to_string(domain_id));
        apply_udp_only_transport(qos);
        log_transport_cfg(domain_id, "fallback_udp_only", qos, disable_shm_env, force_udp_env);
        return factory->create_participant(domain_id, qos, nullptr, StatusMask::none());
    }
}

} // namespace

void load_fastdds_profiles_from_env_once() {
    static std::once_flag once;
    std::call_once(once, []() {
        const char* path = std::getenv("WXZ_FASTDDS_PROFILES_FILE");
        if (path && *path) {
            std::ifstream ifs(path);
            if (!ifs.good()) {
                throw std::runtime_error(std::string("WXZ_FASTDDS_PROFILES_FILE not readable: ") + path);
            }

            auto ret = eprosima::fastdds::dds::DomainParticipantFactory::get_instance()->load_XML_profiles_file(path);
            if (ret != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
                throw std::runtime_error(std::string("FastDDS load_XML_profiles_file failed for: ") + path);
            }
            profiles_state().used_env_file = true;
            return;
        }

        // 未通过环境变量覆盖时：尽力从 install-tree 加载 release 默认 profiles。
        const std::string default_path = default_profiles_path_from_install_prefix();
        if (default_path.empty()) return;
        std::ifstream ifs(default_path);
        if (!ifs.good()) return;

        auto ret = eprosima::fastdds::dds::DomainParticipantFactory::get_instance()->load_XML_profiles_file(default_path);
        if (ret == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            profiles_state().used_default_file = true;
        }
    });
}

[[nodiscard]] eprosima::fastdds::dds::DomainParticipant* create_fastdds_participant_from_env(int domain_id) {
    using namespace eprosima::fastdds::dds;

    load_fastdds_profiles_from_env_once();

    std::string participant_profile;
    if (const char* v = std::getenv("WXZ_FASTDDS_PARTICIPANT_PROFILE")) {
        participant_profile = v;
    }

    wxz::core::Logger::getInstance().info(std::string("FastDDS participant config") +
                                         " domain=" + std::to_string(domain_id) +
                                         " profile=" + (participant_profile.empty() ? "<auto>" : participant_profile) +
                                         " profiles_env_file=" + (profiles_state().used_env_file ? "1" : "0") +
                                         " profiles_default_file=" + (profiles_state().used_default_file ? "1" : "0"));

    if (participant_profile == kStrictParticipantProfile) {
        const char* ros_ds = std::getenv("ROS_DISCOVERY_SERVER");
        if (!ros_ds || !*ros_ds) {
            throw std::runtime_error(
                "WXZ_FASTDDS_PARTICIPANT_PROFILE=wxz_release_participant_strict requires ROS_DISCOVERY_SERVER to be set "
                "(e.g. '127.0.0.1:11811' or '10.0.0.1:11811;10.0.0.2:11811').");
        }
    }

    auto* factory = DomainParticipantFactory::get_instance();

    if (!participant_profile.empty()) {
        // 预先检查 profile 是否存在：让失败更明确，并减少噪声日志。
        DomainParticipantQos qos;
        const auto ret = factory->get_participant_qos_from_profile(participant_profile, qos);
        if (ret != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            throw std::runtime_error("FastDDS participant profile not found: " + participant_profile);
        }

        // 严格 profile：强制 Discovery Server 客户端行为，远端服务器列表来自 ROS_DISCOVERY_SERVER。
        // 这可以避免依赖隐式的 SIMPLE->CLIENT 转换。
        if (participant_profile == kStrictParticipantProfile) {
            auto& servers = qos.wire_protocol().builtin.discovery_config.m_DiscoveryServers;
            if (servers.empty()) {
                (void)eprosima::fastrtps::rtps::load_environment_server_info(servers);
            }
            if (servers.empty()) {
                throw std::runtime_error(
                    "ROS_DISCOVERY_SERVER did not yield any Discovery Server locators; please set it to a non-empty "
                    "semicolon-separated list like '10.0.0.1:11811;10.0.0.2:11811'.");
            }
        }

        return create_participant_with_fallback(domain_id, factory, qos);
    }

    // 未显式指定 participant profile：若已加载 install-tree 的 release 默认 profiles，则选择已知默认 profile。
    if (profiles_state().used_default_file && !profiles_state().used_env_file) {
        DomainParticipantQos qos;
        const auto ret = factory->get_participant_qos_from_profile(kDefaultParticipantProfile, qos);
        if (ret == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            return create_participant_with_fallback(domain_id, factory, qos);
        }
        // 若因任何原因未找到 profile，则继续走默认路径。
    }

    // 让 XML 默认 profile（若存在）生效。
    DomainParticipantQos qos = PARTICIPANT_QOS_DEFAULT;
    return create_participant_with_fallback(domain_id, factory, qos);
}

} // namespace wxz::core::internal
