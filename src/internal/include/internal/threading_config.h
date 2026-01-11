#pragma once
#include <string>

// 获取指定模块的线程数。优先级：环境变量覆盖 > 本地配置文件 > 默认值
#include "internal/config.h"
inline int get_thread_count_for_module(const std::string &module, int default_n, int max_n) {
    return Config::getInstance().getThreadCount(module, default_n, max_n);
}
