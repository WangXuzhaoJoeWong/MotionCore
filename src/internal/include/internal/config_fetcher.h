#pragma once

#include <string>
#include <unordered_map>

// 使用 libcurl 的轻量 HTTP 配置拉取器。
// 期望 payload：文本 body，每行 `key=value`；忽略空行。
// 返回 key/value map；失败则返回空 map，并把错误写到 stderr。
std::unordered_map<std::string, std::string> fetch_kv_over_http(const std::string& url, long timeout_ms = 2000);
