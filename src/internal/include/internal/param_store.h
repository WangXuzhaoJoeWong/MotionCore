// ParamStore: thread-safe in-memory parameter snapshot for BT nodes and other modules.
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
