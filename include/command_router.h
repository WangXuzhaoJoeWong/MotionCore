#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "kv_codec.h"

namespace wxz::core {

// 面向 KV payload 的最小命令路由器。
// - 若存在 "op"：按 op 分发
// - 否则：分发到可选的默认 handler
// - 按路由校验必填字段，并在缺失/未知时调用回调
class CommandRouter {
public:
    using KvMap = KvCodec::KvMap;
    using Handler = std::function<void(const KvMap&)>;

    struct Route {
        std::vector<std::string> required;
        Handler handler;
    };

    // 当 kv 对于“已知路由”缺少必填字段时调用。
    // 若 missing_key 为 "op"，则 op 可能为空。
    std::function<void(std::string_view op, std::string_view missing_key, const KvMap& kv)> on_missing_field;

    // 当 kv 有 op 但找不到匹配路由时调用。
    std::function<void(std::string_view op, const KvMap& kv)> on_unknown_op;

    // 当 kv 没有 op 且未配置默认 handler 时调用。
    std::function<void(const KvMap& kv)> on_missing_op;

    void set_default(std::initializer_list<const char*> required, Handler handler) {
        default_.required.clear();
        default_.required.reserve(required.size());
        for (auto* k : required) default_.required.emplace_back(k);
        default_.handler = std::move(handler);
        has_default_ = true;
    }

    void add_route(std::string op, std::initializer_list<const char*> required, Handler handler) {
        Route r;
        r.required.reserve(required.size());
        for (auto* k : required) r.required.emplace_back(k);
        r.handler = std::move(handler);
        routes_.emplace(std::move(op), std::move(r));
    }

    void dispatch(std::string_view payload) const {
        const KvMap kv = KvCodec::parse(payload);
        dispatch_kv(kv);
    }

    void dispatch_kv(const KvMap& kv) const {
        const std::string op = KvCodec::get(kv, "op", "");

        if (op.empty()) {
            if (has_default_) {
                if (!check_required("", default_, kv)) return;
                default_.handler(kv);
            } else {
                if (on_missing_op) on_missing_op(kv);
                else if (on_missing_field) on_missing_field("", "op", kv);
            }
            return;
        }

        auto it = routes_.find(op);
        if (it == routes_.end()) {
            if (on_unknown_op) on_unknown_op(op, kv);
            return;
        }

        if (!check_required(op, it->second, kv)) return;
        it->second.handler(kv);
    }

private:
    bool check_required(std::string_view op, const Route& r, const KvMap& kv) const {
        for (const auto& k : r.required) {
            if (!KvCodec::has(kv, k.c_str()) || KvCodec::get(kv, k.c_str(), "").empty()) {
                if (on_missing_field) on_missing_field(op, k, kv);
                return false;
            }
        }
        return true;
    }

    bool has_default_{false};
    Route default_{};
    std::unordered_map<std::string, Route> routes_;
};

} // namespace wxz::core
