#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "dto/event_dto.h"

namespace wxz::framework {

/// 统一的业务状态/错误表示。
///
/// 设计目标：
/// - 业务侧只处理“成功/失败 + 原因”，避免到处拼 KV 字段。
/// - 与现有 KV 口径兼容（ok/err_code/err/sdk_code，以及历史 code）。
struct Status {
    bool ok{true};

    // 稳定错误码：建议业务层/服务层使用。
    int err_code{0};

    // 稳定短字符串 token（snake_case），用于日志/上报/前端展示。
    std::string err;

    // 可选：SDK/底层返回码（与具体设备 SDK 绑定）。
    std::optional<int> sdk_code;

    static Status ok_status() { return Status{}; }

    static Status error(int code, std::string_view reason, std::optional<int> sdk = std::nullopt) {
        Status s;
        s.ok = false;
        s.err_code = code;
        s.err = std::string(reason);
        s.sdk_code = sdk;
        return s;
    }

    /// 从 KV 恢复状态（兼容历史字段）。
    static Status from_kv(const EventDTOUtil::KvMap& kv) {
        auto get = [&](std::string_view key) -> std::string_view {
            auto it = kv.find(std::string(key));
            if (it == kv.end()) return {};
            return it->second;
        };

        auto parse_int = [&](std::string_view s) -> std::optional<int> {
            if (s.empty()) return std::nullopt;
            try {
                return std::stoi(std::string(s));
            } catch (...) {
                return std::nullopt;
            }
        };

        auto parse_ok = [&](std::string_view s) -> std::optional<bool> {
            if (s.empty()) return std::nullopt;
            if (s == "1" || s == "true" || s == "TRUE") return true;
            if (s == "0" || s == "false" || s == "FALSE") return false;
            return std::nullopt;
        };

        Status st;

        if (auto ok = parse_ok(get("ok")); ok.has_value()) {
            st.ok = *ok;
        }

        // 新字段优先。
        if (auto code = parse_int(get("err_code")); code.has_value()) {
            st.err_code = *code;
        }

        st.err = std::string(get("err"));

        // sdk_code 为可选。
        if (auto sdk = parse_int(get("sdk_code")); sdk.has_value()) {
            st.sdk_code = *sdk;
        } else if (auto legacy = parse_int(get("code")); legacy.has_value()) {
            // 兼容历史：code 常用于承载 SDK code。
            st.sdk_code = *legacy;
        }

        // 若 ok=false 但 err_code 未提供，则按保守策略置 1。
        if (!st.ok && st.err_code == 0) st.err_code = 1;
        return st;
    }

    /// 写回 KV（会覆盖 ok/err_code/err/sdk_code，并保留历史 code 兼容）。
    void apply_to(EventDTOUtil::KvMap& kv) const {
        kv["ok"] = ok ? "1" : "0";
        kv["err_code"] = std::to_string(err_code);
        if (ok) {
            kv.erase("err");
        } else {
            kv["err"] = err;
        }

        if (sdk_code.has_value()) {
            kv["sdk_code"] = std::to_string(*sdk_code);
            // 兼容历史字段。
            kv["code"] = std::to_string(*sdk_code);
        }
    }
};

} // namespace wxz::framework
