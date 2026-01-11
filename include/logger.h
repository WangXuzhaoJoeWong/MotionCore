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

// A lightweight logger intended for both MotionCore internals and Workstation services.
//
// Output format (single line):
//   <prefix><tag> <message> key=value ...
//
// - prefix is user-provided (e.g. "[wxz_bt_service] ")
// - tag is one of [ERR]/[WRN]/[INF]/[DBG]
// - fields are optional key/value pairs for correlation (e.g. trace_id)
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
