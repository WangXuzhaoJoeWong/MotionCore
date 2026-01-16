// ParamStore：线程安全的内存参数快照，供 BT 节点等模块使用。
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>

class ParamStore {
public:
    static ParamStore& instance();

    void set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key) const;

private:
    ParamStore() = default;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::string> data_;
};
