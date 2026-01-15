#include "fault_recovery_executor.h"

#include "service_common.h"
#include "observability.h"
#include "executor.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace wxz::core {

FaultRecoveryExecutor::FaultRecoveryExecutor(int domain,
                                             std::string topic,
                                             std::vector<FaultRecoveryRule> rules,
                                             RequestRestartFn request_restart,
                                             WarnFn warn)
    : domain_(domain),
      topic_(std::move(topic)),
      rules_(std::move(rules)),
      request_restart_(std::move(request_restart)),
      warn_(std::move(warn)) {}

void FaultRecoveryExecutor::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return;

    ChannelQoS qos = default_reliable_qos();
    qos.history = 16;

    constexpr std::size_t kMaxPayload = 4096;
    sub_ = std::make_unique<FastddsChannel>(domain_, topic_, qos, kMaxPayload, /*enable_pub=*/false, /*enable_sub=*/true);

    // Pre-register minimal metrics so /metrics has stable keys.
    metrics().gauge_set("wxz_fault_recovery_enabled", 1.0, {});
    metrics().counter_add("wxz_fault_recovery_actions_total", 0.0, {{"action", "degrade"}});
    metrics().counter_add("wxz_fault_recovery_actions_total", 0.0, {{"action", "restart"}});

    sub_token_ = sub_->subscribe_scoped(
        [this](const std::uint8_t* data, std::size_t size) { handle_message(data, size); },
        this);
}

void FaultRecoveryExecutor::start_on(Executor& ex) {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return;

    ChannelQoS qos = default_reliable_qos();
    qos.history = 16;

    constexpr std::size_t kMaxPayload = 4096;
    sub_ = std::make_unique<FastddsChannel>(domain_, topic_, qos, kMaxPayload, /*enable_pub=*/false, /*enable_sub=*/true);

    // Pre-register minimal metrics so /metrics has stable keys.
    metrics().gauge_set("wxz_fault_recovery_enabled", 1.0, {});
    metrics().counter_add("wxz_fault_recovery_actions_total", 0.0, {{"action", "degrade"}});
    metrics().counter_add("wxz_fault_recovery_actions_total", 0.0, {{"action", "restart"}});

    sub_token_ = sub_->subscribe_scoped_on(
        ex,
        [this](const std::uint8_t* data, std::size_t size) { handle_message(data, size); },
        this);
}

void FaultRecoveryExecutor::stop() {
    if (!started_.exchange(false)) return;

    metrics().gauge_set("wxz_fault_recovery_enabled", 0.0, {});

    sub_token_.reset();
    if (sub_) {
        sub_->stop();
        sub_.reset();
    }
}

bool FaultRecoveryExecutor::is_active(const EventDTOUtil::KvMap& kv) {
    auto it = kv.find("active");
    if (it == kv.end()) return false;
    const auto& v = it->second;
    return v == "1" || v == "true" || v == "TRUE";
}

bool FaultRecoveryExecutor::match_rule(const FaultRecoveryRule& r, const EventDTOUtil::KvMap& kv) const {
    if (!r.fault.empty()) {
        auto it = kv.find("fault");
        if (it == kv.end() || it->second != r.fault) return false;
    }
    if (!r.service.empty()) {
        auto it = kv.find("service");
        if (it == kv.end() || it->second != r.service) return false;
    }
    if (!r.severity.empty()) {
        auto it = kv.find("severity");
        if (it == kv.end() || it->second != r.severity) return false;
    }
    return true;
}

bool FaultRecoveryExecutor::write_marker_file(const std::string& path, const std::string& contents) {
    if (path.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(path);
    auto parent = p.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
    }
    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << contents;
    ofs.flush();
    return static_cast<bool>(ofs);
}

void FaultRecoveryExecutor::handle_message(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return;

    const std::string raw(reinterpret_cast<const char*>(data), size);
    auto kv = EventDTOUtil::parsePayloadKv(raw);

    if (kv.count("kind") && kv["kind"] != "fault") return;
    if (!is_active(kv)) return;

    for (const auto& r : rules_) {
        if (!match_rule(r, kv)) continue;

        if (r.action == "degrade") {
            if (!degraded_.exchange(true)) {
                const std::string service = kv.count("service") ? kv["service"] : "";
                const std::string fault = kv.count("fault") ? kv["fault"] : "";
                const std::string contents = "degraded=1\nservice=" + service + "\nfault=" + fault + "\n";
                const bool ok = write_marker_file(r.marker_file, contents);
                if (!ok && warn_) warn_("fault_recovery degrade: marker_file write failed: '" + r.marker_file + "'");

                metrics().counter_add("wxz_fault_recovery_actions_total", 1.0, {{"action", "degrade"}});
                metrics().gauge_set("wxz_fault_recovery_degraded", 1.0, {});
            }
            return;
        }

        if (r.action == "restart") {
            const int code = (r.exit_code == 0) ? 42 : r.exit_code;
            if (warn_) {
                const std::string service = kv.count("service") ? kv["service"] : "";
                const std::string fault = kv.count("fault") ? kv["fault"] : "";
                warn_("fault_recovery restart: service='" + service + "' fault='" + fault + "' exit_code=" + std::to_string(code));
            }

            metrics().counter_add("wxz_fault_recovery_actions_total", 1.0, {{"action", "restart"}});

            if (request_restart_) request_restart_(code);
            return;
        }
    }
}

} // namespace wxz::core
