#include "logger.h"

#include <cstdlib>
#include <iostream>

namespace wxz::core {

LogLevel parse_log_level(std::string_view s, LogLevel def) {
    if (s == "0" || s == "error" || s == "ERROR") return LogLevel::Error;
    if (s == "1" || s == "warn" || s == "WARN") return LogLevel::Warn;
    if (s == "2" || s == "info" || s == "INFO") return LogLevel::Info;
    if (s == "3" || s == "debug" || s == "DEBUG") return LogLevel::Debug;
    return def;
}

const char* log_level_tag(LogLevel l) {
    switch (l) {
    case LogLevel::Error: return "[ERR]";
    case LogLevel::Warn: return "[WRN]";
    case LogLevel::Debug: return "[DBG]";
    case LogLevel::Info:
    default: return "[INF]";
    }
}

Logger::Logger(LogLevel level, std::string prefix) : level_(level), prefix_(std::move(prefix)) {}

Logger& Logger::getInstance() {
    static Logger instance(parse_log_level(std::getenv("WXZ_LOG_LEVEL") ? std::getenv("WXZ_LOG_LEVEL") : "info",
                                           LogLevel::Info),
                           "");
    return instance;
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

LogLevel Logger::level() const {
    return level_;
}

void Logger::set_prefix(std::string prefix) {
    prefix_ = std::move(prefix);
}

const std::string& Logger::prefix() const {
    return prefix_;
}

void Logger::log(LogLevel l, std::string_view msg) const {
    log(l, msg, {});
}

void Logger::log(LogLevel l, std::string_view msg, std::initializer_list<Field> fields) const {
    if (static_cast<int>(l) > static_cast<int>(level_)) return;
    std::ostream& os = (l == LogLevel::Error) ? std::cerr : std::cout;
    os << prefix_ << log_level_tag(l) << " " << msg;
    for (const auto& [k, v] : fields) {
        if (k.empty()) continue;
        os << " " << k;
        os << "=";
        os << v;
    }
    os << "\n";
}

}  // namespace wxz::core
