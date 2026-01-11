#include "internal/dds_security_precheck.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace wxz::core::internal {

namespace {

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\n' && c != '\r'; };
    while (!s.empty() && !not_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && !not_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string extract_first_tag_value(const std::string& block, const char* tag) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    auto a = block.find(open);
    if (a == std::string::npos) return {};
    a += open.size();
    auto b = block.find(close, a);
    if (b == std::string::npos) return {};
    return trim_copy(block.substr(a, b - a));
}

struct XmlProperty {
    std::string name;
    std::string value;
};

std::vector<XmlProperty> parse_fastdds_properties(const std::string& xml) {
    std::vector<XmlProperty> out;
    std::size_t pos = 0;
    while (true) {
        auto p0 = xml.find("<property", pos);
        if (p0 == std::string::npos) break;
        auto gt = xml.find('>', p0);
        if (gt == std::string::npos) break;
        auto p1 = xml.find("</property>", gt + 1);
        if (p1 == std::string::npos) break;
        const std::string block = xml.substr(gt + 1, p1 - (gt + 1));
        XmlProperty p;
        p.name = extract_first_tag_value(block, "name");
        p.value = extract_first_tag_value(block, "value");
        if (!p.name.empty()) out.push_back(std::move(p));
        pos = p1 + std::string("</property>").size();
    }
    return out;
}

bool ends_with(const std::string& s, const char* suffix) {
    const std::string suf(suffix);
    if (s.size() < suf.size()) return false;
    return s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::filesystem::path normalize_file_uri_to_path(std::string value, const std::filesystem::path& base_dir) {
    value = trim_copy(std::move(value));
    if (value.rfind("file:", 0) == 0) {
        value.erase(0, 5);
        // Handle common file URI forms: file:/abs/path and file:///abs/path
        if (value.rfind("///", 0) == 0) {
            value.erase(0, 2); // "///home" -> "/home"
        }
        std::filesystem::path p(value);
        if (p.is_relative()) p = base_dir / p;
        return p;
    }
    std::filesystem::path p(value);
    if (p.is_relative()) p = base_dir / p;
    return p;
}

} // namespace

DdsSecurityEnvInfo precheck_dds_security_from_fastdds_env_file(const char* env_file) {
    DdsSecurityEnvInfo info;
    if (!env_file || !*env_file) return info;

    const std::filesystem::path env_path(env_file);
    std::ifstream ifs(env_path);
    if (!ifs.good()) {
        throw std::runtime_error(std::string("FASTDDS_ENVIRONMENT_FILE not readable: ") + env_path.string());
    }

    std::string xml((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    const auto props = parse_fastdds_properties(xml);

    bool any_security = false;
    bool auth_enabled = false;
    bool access_enabled = false;
    for (const auto& p : props) {
        if (p.name.rfind("dds.sec.", 0) == 0) any_security = true;
        if (p.name == "dds.sec.auth.plugin") auth_enabled = true;
        if (p.name == "dds.sec.access.plugin") access_enabled = true;
    }

    info.security_enabled = any_security;
    if (!any_security) return info;

    bool has_identity_ca = false;
    bool has_identity_cert = false;
    bool has_private_key = false;
    bool has_permissions_ca = false;
    bool has_governance = false;
    bool has_permissions = false;

    const std::filesystem::path base_dir = env_path.parent_path();
    for (const auto& p : props) {
        if (p.name.rfind("dds.sec.", 0) != 0) continue;

        if (ends_with(p.name, ".identity_ca")) {
            has_identity_ca = true;
        } else if (ends_with(p.name, ".identity_certificate")) {
            has_identity_cert = true;
        } else if (ends_with(p.name, ".private_key")) {
            has_private_key = true;
        } else if (ends_with(p.name, ".permissions_ca")) {
            has_permissions_ca = true;
        } else if (ends_with(p.name, ".governance")) {
            has_governance = true;
        } else if (ends_with(p.name, ".permissions")) {
            has_permissions = true;
        } else {
            continue;
        }

        if (p.value.empty()) {
            throw std::runtime_error(std::string("DDS-Security misconfigured: empty value for ") + p.name +
                                     " (in FASTDDS_ENVIRONMENT_FILE=" + env_path.string() + ")");
        }

        const auto fpath = normalize_file_uri_to_path(p.value, base_dir);
        std::error_code ec;
        const bool ok = std::filesystem::exists(fpath, ec) && std::filesystem::is_regular_file(fpath, ec);
        if (!ok) {
            throw std::runtime_error(std::string("DDS-Security missing file for ") + p.name + ": " + fpath.string() +
                                     " (from FASTDDS_ENVIRONMENT_FILE=" + env_path.string() + ")");
        }
    }

    if (auth_enabled && (!has_identity_ca || !has_identity_cert || !has_private_key)) {
        throw std::runtime_error(std::string("DDS-Security misconfigured: auth enabled but identity artifacts missing") +
                                 " (need identity_ca/identity_certificate/private_key in FASTDDS_ENVIRONMENT_FILE=" +
                                 env_path.string() + ")");
    }

    if (access_enabled && (!has_permissions_ca || !has_governance || !has_permissions)) {
        throw std::runtime_error(
            std::string("DDS-Security misconfigured: access enabled but governance/permissions artifacts missing") +
            " (need permissions_ca/governance/permissions in FASTDDS_ENVIRONMENT_FILE=" + env_path.string() + ")");
    }

    return info;
}

} // namespace wxz::core::internal
