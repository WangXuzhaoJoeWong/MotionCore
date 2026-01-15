#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "rpc_common.h"

namespace wxz::core {
class Executor;
class Strand;
}

namespace wxz::core::rpc {

// 基于 FastddsChannel 的最小 RPC Server。
// 请求/响应采用 JSON 文本：
// - 请求：{"op":"...","id":"...","ts_ms":123,"params":{...}}
// - 响应：{"op":"...","id":"...","status":"ok|error","ts_ms":456,"reason":"...","result":{...}}
class RpcServer {
public:
    using Json = nlohmann::json;

    struct Reply {
        bool ok{true};
        std::string reason; // ok=false 时必填
        Json result = Json::object();
    };

    using Handler = std::function<Reply(const Json& params)>;

    explicit RpcServer(RpcServerOptions opts);
    ~RpcServer();

    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;

    // 将接收回调投递到 executor/strand（避免占用 DDS listener 线程）。
    void bind_scheduler(Executor& ex);
    void bind_scheduler(Strand& strand);

    void add_handler(std::string op, Handler handler);

    bool start();
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wxz::core::rpc
