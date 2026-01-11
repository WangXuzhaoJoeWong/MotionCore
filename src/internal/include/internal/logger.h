#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>

// 简单的 Logger（单例模式）
class Logger {
public:
    static Logger& getInstance();
    void log(const std::string& msg);

private:
    Logger() = default;
};

#endif // LOGGER_H
