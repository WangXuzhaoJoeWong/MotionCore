#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "dto/event_dto.h"
#include "fastdds_channel.h"
#include "subscription.h"

namespace wxz::core {

struct FaultRecoveryRule {
    std::string fault;
    std::string service;
    std::string severity;

    // 恢复动作：restart|degrade
    std::string action;

    // 当 action=restart 时：使用该退出码请求重启
    int exit_code{42};

    // 当 action=degrade 时：写入该 marker 文件
    std::string marker_file;
};

class FaultRecoveryExecutor {
public:
    using WarnFn = std::function<void(const std::string&)>;
    using RequestRestartFn = std::function<void(int exit_code)>;

    FaultRecoveryExecutor(int domain,
                         std::string topic,
                         std::vector<FaultRecoveryRule> rules,
                         RequestRestartFn request_restart,
                         WarnFn warn);

    FaultRecoveryExecutor(const FaultRecoveryExecutor&) = delete;
    FaultRecoveryExecutor& operator=(const FaultRecoveryExecutor&) = delete;

    void start();
    void stop();

private:
    static bool is_active(const EventDTOUtil::KvMap& kv);
    bool match_rule(const FaultRecoveryRule& r, const EventDTOUtil::KvMap& kv) const;
    void handle_message(const std::uint8_t* data, std::size_t size);

    static bool write_marker_file(const std::string& path, const std::string& contents);

    int domain_{0};
    std::string topic_;
    std::vector<FaultRecoveryRule> rules_;

    RequestRestartFn request_restart_;
    WarnFn warn_;

    std::atomic<bool> started_{false};
    std::atomic<bool> degraded_{false};

    std::unique_ptr<FastddsChannel> sub_;
    Subscription sub_token_;
};

} // namespace wxz::core
