#include "internal/channel_factory.h"

#include <iostream>

namespace channel_factory {

namespace {
constexpr std::size_t kMaxPayloadGuard = 1 * 1024 * 1024; // 1MB upper bound
constexpr std::size_t kMaxHistoryGuard = 1024;             // depth upper bound

ChannelQoS guardrail_qos(ChannelQoS qos, const std::string& name) {
    if (qos.history > kMaxHistoryGuard) {
        std::cerr << "[channel_factory] clamp history for " << name << " to " << kMaxHistoryGuard << "\n";
        qos.history = kMaxHistoryGuard;
    }
    return qos;
}

bool guardrail_payload(std::size_t payload, const std::string& name) {
    if (payload > kMaxPayloadGuard) {
        std::cerr << "[channel_factory] reject channel " << name << " max_payload " << payload
                  << " exceeds guard " << kMaxPayloadGuard << "\n";
        return false;
    }
    return true;
}

bool allowed_by_filters(const Config& cfg, const std::string& name) {
    const auto& allow = cfg.getChannelAllowlist();
    const auto& deny = cfg.getChannelDenylist();
    if (!deny.empty()) {
        if (std::find(deny.begin(), deny.end(), name) != deny.end()) {
            std::cerr << "[channel_factory] deny channel " << name << " by denylist\n";
            return false;
        }
    }
    if (!allow.empty()) {
        if (std::find(allow.begin(), allow.end(), name) == allow.end()) {
            std::cerr << "[channel_factory] skip channel " << name << " not in allowlist\n";
            return false;
        }
    }
    return true;
}
} // namespace

std::map<std::string, std::shared_ptr<wxz::core::FastddsChannel>>
build_fastdds_channels_from_config(const Config& cfg) {
    std::map<std::string, std::shared_ptr<wxz::core::FastddsChannel>> out;
    for (const auto& kv : cfg.getChannels()) {
        const auto& c = kv.second;
        if (c.transport != "fastdds") continue;
        if (!allowed_by_filters(cfg, c.name)) continue;
        if (c.topic.empty()) {
            std::cerr << "[channel_factory] skip fastdds channel without topic: " << c.name << "\n";
            continue;
        }
        if (!guardrail_payload(c.max_payload, c.name)) continue;
        ChannelQoS qos = c.qos;
        if (cfg.isRealtimeMode() && !qos.realtime_hint) {
            qos = ChannelQoS::realtime_preset(qos.history == 0 ? 8 : qos.history);
            if (qos.deadline_ns == 0) qos.deadline_ns = 2'000'000; // tighten default deadline in realtime
            if (qos.latency_budget_ns == 0) qos.latency_budget_ns = 1'000'000;
        }
        qos = guardrail_qos(qos, c.name);
        try {
            auto ch = std::make_shared<wxz::core::FastddsChannel>(c.domain, c.topic, qos, c.max_payload);
            out.emplace(c.name, std::move(ch));
        } catch (const std::exception& ex) {
            std::cerr << "[channel_factory] failed to create fastdds channel " << c.name << ": " << ex.what() << "\n";
        }
    }
    return out;
}

std::map<std::string, std::shared_ptr<wxz::core::ShmChannel>>
build_shm_channels_from_config(const Config& cfg, bool create) {
    std::map<std::string, std::shared_ptr<wxz::core::ShmChannel>> out;
    for (const auto& kv : cfg.getChannels()) {
        const auto& c = kv.second;
        if (c.transport != "shm") continue;
        if (!allowed_by_filters(cfg, c.name)) continue;

        if (c.shm_name.empty()) {
            std::cerr << "[channel_factory] skip shm channel without shm.name: " << c.name << "\n";
            continue;
        }
        if (c.shm_capacity == 0 || c.shm_slot_size == 0) {
            std::cerr << "[channel_factory] skip shm channel with invalid capacity/slot_size: " << c.name << "\n";
            continue;
        }

        // 保护阈值（复用 payload guard 作为 slot_size 上限）
        if (!guardrail_payload(c.shm_slot_size, c.name)) continue;
        try {
            auto ch = std::make_shared<wxz::core::ShmChannel>(c.shm_name, c.shm_capacity, c.shm_slot_size, create);
            out.emplace(c.name, std::move(ch));
        } catch (const std::exception& ex) {
            std::cerr << "[channel_factory] failed to create shm channel " << c.name << ": " << ex.what() << "\n";
        }
    }
    return out;
}

} // namespace channel_factory
