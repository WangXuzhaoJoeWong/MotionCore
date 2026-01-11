#include "dto/event_dto.h"

#include <chrono>
#include <random>
#include <sstream>

EventDTOUtil::KvMap EventDTOUtil::parsePayloadKv(const std::string& payload) {
    KvMap result;
    std::stringstream ss(payload);
    std::string kv;
    while (std::getline(ss, kv, ';')) {
        if (kv.empty()) continue;
        auto pos = kv.find('=');
        if (pos == std::string::npos) continue;
        auto key = kv.substr(0, pos);
        auto val = kv.substr(pos + 1);
        if (key.empty()) continue;
        result.emplace(std::move(key), std::move(val));
    }
    return result;
}

std::string EventDTOUtil::buildPayloadKv(const KvMap& kvs) {
    std::string out;
    bool first = true;
    for (const auto& [key, value] : kvs) {
        if (key.empty()) continue;
        if (!first) {
            out.push_back(';');
        }
        first = false;
        out.append(key);
        out.push_back('=');
        out.append(value);
    }
    return out;
}

void EventDTOUtil::fillMeta(EventDTO& dto, const std::string& default_source) {
    // 填充时间戳（毫秒）
    if (dto.timestamp == 0) {
        using namespace std::chrono;
        auto now = time_point_cast<milliseconds>(system_clock::now());
        dto.timestamp = static_cast<std::uint64_t>(now.time_since_epoch().count());
    }

    // 简单生成 event_id：ts-random
    if (dto.event_id.empty()) {
        static thread_local std::mt19937_64 rng{
            static_cast<std::mt19937_64::result_type>(std::random_device{}())};
        std::uniform_int_distribution<std::uint64_t> dist;
        auto rnd = dist(rng);
        dto.event_id = std::to_string(dto.timestamp) + "-" + std::to_string(rnd);
    }

    // 填充来源
    if (dto.source.empty() && !default_source.empty()) {
        dto.source = default_source;
    }
}
