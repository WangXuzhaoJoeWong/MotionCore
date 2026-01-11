#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace wxz::core::rpc {

using Json = nlohmann::json;

inline std::optional<Json> parseJsonObject(std::string_view text) {
    Json j = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    return j;
}

inline std::optional<std::string> getOptionalString(const Json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

inline std::string buildErrorResponse(const std::string& op,
                                     const std::string& id,
                                     long long ts_ms,
                                     const std::string& reason) {
    Json resp = Json::object();
    resp["op"] = op;
    if (!id.empty()) {
        resp["id"] = id;
    }
    resp["status"] = "error";
    resp["ts_ms"] = ts_ms;
    resp["reason"] = reason;
    return resp.dump();
}

inline std::string buildOkResponse(const std::string& op,
                                  const std::string& id,
                                  long long ts_ms,
                                  std::size_t count,
                                  const Json& params_obj) {
    Json resp = Json::object();
    resp["op"] = op;
    if (!id.empty()) {
        resp["id"] = id;
    }
    resp["status"] = "ok";
    resp["ts_ms"] = ts_ms;
    resp["count"] = count;
    resp["params"] = params_obj;
    return resp.dump();
}

}  // namespace wxz::core::rpc
