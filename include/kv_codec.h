#pragma once

#include <string>
#include <string_view>

#include "dto/event_dto.h"

namespace wxz::core {

// 当前 KV payload 格式（"k=v;k2=v2"）的轻量封装。
// 在集中管理解析/构造逻辑的同时，保持既有 wire 格式稳定。
struct KvCodec {
    using KvMap = EventDTOUtil::KvMap;

    static inline KvMap parse(std::string_view payload) {
        return EventDTOUtil::parsePayloadKv(std::string(payload));
    }

    static inline std::string build(const KvMap& kv) {
        return EventDTOUtil::buildPayloadKv(kv);
    }

    static inline std::string get(const KvMap& kv, const char* key, const std::string& def = {}) {
        auto it = kv.find(key);
        if (it == kv.end()) return def;
        return it->second;
    }

    static inline bool has(const KvMap& kv, const char* key) {
        return kv.find(key) != kv.end();
    }
};

} // namespace wxz::core
