#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "rpc_common.h"

namespace wxz::core {
class Executor;
class Strand;
}

namespace wxz::core::rpc {

class RpcClient {
public:
    using Json = nlohmann::json;

    struct Result {
        RpcErrorCode code{RpcErrorCode::Ok};
        std::string reason;
        Json result = Json::object();

        bool ok() const { return code == RpcErrorCode::Ok; }
    };

    explicit RpcClient(RpcClientOptions opts);
    ~RpcClient();

    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    void bind_scheduler(Executor& ex);
    void bind_scheduler(Strand& strand);

    bool start();
    void stop();

    // 同步调用：发送请求并等待响应。
    Result call(const std::string& op,
                const Json& params,
                std::chrono::milliseconds timeout);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wxz::core::rpc
