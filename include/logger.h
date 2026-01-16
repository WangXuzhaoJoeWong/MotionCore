#pragma once

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <iosfwd>
#include <string>
#include <string_view>
#include <utility>

namespace wxz::core {

enum class LogLevel : int { Error = 0, Warn = 1, Info = 2, Debug = 3 };

LogLevel parse_log_level(std::string_view s, LogLevel def = LogLevel::Info);
const char* log_level_tag(LogLevel l);

// 轻量日志组件：同时面向 MotionCore 内部与 Workstation 服务。
//
// 输出格式（单行）：
//   <prefix><tag> <message> key=value ...
//
// - prefix：用户提供的前缀（例如 "[wxz_bt_service] "）
// - tag： [ERR]/[WRN]/[INF]/[DBG] 之一
// - fields：可选的 key/value 字段，用于关联分析（例如 trace_id）
class Logger {
public:
    using Field = std::pair<std::string_view, std::string_view>;

    Logger() = default;
    Logger(LogLevel level, std::string prefix);

    static Logger& getInstance();

    void set_level(LogLevel level);
    LogLevel level() const;

    void set_prefix(std::string prefix);
    const std::string& prefix() const;

    void log(LogLevel l, std::string_view msg) const;
    void log(LogLevel l, std::string_view msg, std::initializer_list<Field> fields) const;

    void error(std::string_view msg) const { log(LogLevel::Error, msg); }
    void warn(std::string_view msg) const { log(LogLevel::Warn, msg); }
    void info(std::string_view msg) const { log(LogLevel::Info, msg); }
    void debug(std::string_view msg) const { log(LogLevel::Debug, msg); }

private:
    LogLevel level_{LogLevel::Info};
    std::string prefix_;
};

}  // namespace wxz::core
