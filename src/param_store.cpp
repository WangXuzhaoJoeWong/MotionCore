#include "internal/param_store.h"

ParamStore& ParamStore::instance() {
    static ParamStore inst;
    return inst;
}

void ParamStore::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mtx_);
    data_[key] = value;
}

std::optional<std::string> ParamStore::get(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    return it->second;
}
