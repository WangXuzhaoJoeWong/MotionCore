#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

#include "clock.h"
#include "inproc_channel.h" // ChannelQoS（通道 QoS 配置）

namespace wxz::core {

inline std::string getenv_str(const char* key, const std::string& def) {
    const char* v = std::getenv(key);
    if (!v || !*v) return def;
    return std::string(v);
}

inline int getenv_int(const char* key, int def) {
    const char* v = std::getenv(key);
    if (!v || !*v) return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

inline std::uint64_t now_epoch_ms() {
    return clock_now_epoch_ms();
}

inline bool write_health_file(const std::string& path, const std::string& service, bool ok) {
    if (path.empty()) return true;
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs) return false;
    ofs << "service=" << service << ";ok=" << (ok ? 1 : 0) << ";ts_ms=" << now_epoch_ms() << "\n";
    return true;
}

inline ChannelQoS default_reliable_qos() {
    ChannelQoS qos;
    qos.reliability = ChannelQoS::Reliability::reliable;
    qos.history = 16;
    qos.deadline_ns = 1'000'000'000;
    qos.latency_budget_ns = 5'000'000;
    qos.async_publish = true;
    return qos;
}

} // namespace wxz::core
