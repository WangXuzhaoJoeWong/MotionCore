#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "framework/service.h"
#include "framework/service_client.h"
#include "framework/status.h"

namespace wxz::framework::typed_rpc {

template <class T>
struct Result {
    Status status;
    T value{};
};

namespace detail {

template <class T>
inline bool try_from_json(const nlohmann::json& j, T& out) {
    try {
        out = j.get<T>();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace detail

// Server side: decode params into Req, run typed handler, encode Resp as result.
// Requires nlohmann::json conversions for Req/Resp (to_json/from_json).
template <class Req, class Resp>
inline void add_handler(RpcService& svc,
                        std::string op,
                        std::function<Result<Resp>(const Req&)> handler) {
    svc.add_handler(std::move(op), [h = std::move(handler)](const RpcService::Json& params) {
        Req req{};
        if (!detail::try_from_json(params, req)) {
            return RpcService::Reply{Status::error(1, "invalid_params"), RpcService::Json::object()};
        }

        Result<Resp> r;
        try {
            r = h(req);
        } catch (...) {
            r.status = Status::error(1, "handler_exception");
        }

        RpcService::Reply rep;
        rep.status = r.status;
        if (r.status.ok) {
            try {
                rep.result = RpcService::Json(r.value);
            } catch (...) {
                rep.status = Status::error(1, "encode_failed");
                rep.result = RpcService::Json::object();
            }
        }
        return rep;
    });
}

// Client side: encode Req as params, call, decode result into Resp.
// Requires nlohmann::json conversions for Req/Resp (to_json/from_json).
template <class Req, class Resp>
inline Result<Resp> call(RpcServiceClient& cli,
                         const std::string& op,
                         const Req& req,
                         std::chrono::milliseconds timeout) {
    RpcServiceClient::Json params;
    try {
        params = RpcServiceClient::Json(req);
    } catch (...) {
        Result<Resp> out;
        out.status = Status::error(1, "encode_failed");
        return out;
    }

    auto rep = cli.call(op, params, timeout);
    Result<Resp> out;
    out.status = rep.status;
    if (!out.status.ok) return out;

    if (!detail::try_from_json(rep.result, out.value)) {
        out.status = Status::error(1, "decode_failed");
    }
    return out;
}

template <class Req, class Resp>
inline Result<Resp> call(RpcServiceClient& cli,
                         const std::string& op,
                         const Req& req) {
    RpcServiceClient::Json params;
    try {
        params = RpcServiceClient::Json(req);
    } catch (...) {
        Result<Resp> out;
        out.status = Status::error(1, "encode_failed");
        return out;
    }

    auto rep = cli.call(op, params);
    Result<Resp> out;
    out.status = rep.status;
    if (!out.status.ok) return out;

    if (!detail::try_from_json(rep.result, out.value)) {
        out.status = Status::error(1, "decode_failed");
    }
    return out;
}

} // namespace wxz::framework::typed_rpc
