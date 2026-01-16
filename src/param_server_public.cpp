#include "param_server.h"

#include "internal/param_store.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wxz::core {

namespace {

std::string paramTypeFromValue(const ParamValue& v) {
    if (std::holds_alternative<int>(v)) {
        return "int";
    }
    if (std::holds_alternative<double>(v)) {
        return "double";
    }
    if (std::holds_alternative<bool>(v)) {
        return "bool";
    }
    return "string";
}

bool valueMatchesType(const ParamValue& v, const std::string& type) {
    if (type == "int") {
        return std::holds_alternative<int>(v);
    }
    if (type == "double") {
        return std::holds_alternative<double>(v);
    }
    if (type == "bool") {
        return std::holds_alternative<bool>(v);
    }
    if (type == "string") {
        return std::holds_alternative<std::string>(v);
    }
    return false;
}

std::string toString(const ParamValue& v) {
    if (auto p = std::get_if<int>(&v)) {
        return std::to_string(*p);
    }
    if (auto p = std::get_if<double>(&v)) {
        return std::to_string(*p);
    }
    if (auto p = std::get_if<bool>(&v)) {
        return *p ? "true" : "false";
    }
    return std::get<std::string>(v);
}

std::optional<ParamValue> parseFromString(const std::string& s, const std::string& type) {
    try {
        if (type == "int") {
            return ParamValue{std::stoi(s)};
        }
        if (type == "double") {
            return ParamValue{std::stod(s)};
        }
        if (type == "bool") {
            if (s == "true" || s == "1") {
                return ParamValue{true};
            }
            if (s == "false" || s == "0") {
                return ParamValue{false};
            }
            return std::nullopt;
        }
        // string 或未知类型
        return ParamValue{s};
    } catch (...) {
        return std::nullopt;
    }
}

std::unordered_map<std::string, std::string> readKeyValueFile(const std::string& path) {
    std::unordered_map<std::string, std::string> kvs;
    std::ifstream ifs(path);
    if (!ifs.good()) {
        return kvs;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= line.size()) {
            continue;
        }
        kvs.emplace(line.substr(0, eq), line.substr(eq + 1));
    }
    return kvs;
}

void writeKeyValueFile(const std::string& path, const std::unordered_map<std::string, std::string>& kvs) {
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.good()) {
        return;
    }
    for (const auto& [k, v] : kvs) {
        ofs << k << '=' << v << '\n';
    }
}

} // namespace

class ParamServer::Impl {
public:
    bool declareParam(ParamDesc desc) {
        if (desc.name.empty()) {
            return false;
        }
        if (desc.type.empty()) {
            desc.type = paramTypeFromValue(desc.default_value);
        }
        if (!valueMatchesType(desc.default_value, desc.type)) {
            return false;
        }

        std::vector<IParamObserver*> to_notify;
        ParamValue value_to_notify{};
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (descs_.count(desc.name) != 0U) {
                return false;
            }
            descs_.emplace(desc.name, desc);
            values_.emplace(desc.name, desc.default_value);
            ParamStore::instance().set(desc.name, toString(desc.default_value));

            auto it = observers_.find(desc.name);
            if (it != observers_.end() && !it->second.empty()) {
                to_notify = it->second;
                value_to_notify = desc.default_value;
            }
        }

        for (auto* obs : to_notify) {
            if (obs != nullptr) {
                obs->onParamChanged(desc.name, value_to_notify);
            }
        }
        return true;
    }

    std::optional<ParamValue> getValue(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = values_.find(key);
        if (it == values_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool setValue(const std::string& key, const ParamValue& value) {
        std::vector<IParamObserver*> to_notify;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto desc_it = descs_.find(key);
            if (desc_it == descs_.end()) {
                return false;
            }
            if (desc_it->second.read_only) {
                return false;
            }
            if (!valueMatchesType(value, desc_it->second.type)) {
                return false;
            }

            values_[key] = value;
            ParamStore::instance().set(key, toString(value));

            auto obs_it = observers_.find(key);
            if (obs_it != observers_.end()) {
                to_notify = obs_it->second;
            }
        }

        for (auto* obs : to_notify) {
            if (obs != nullptr) {
                obs->onParamChanged(key, value);
            }
        }
        return true;
    }

    void subscribeKey(const std::string& key, IParamObserver* observer) {
        if (observer == nullptr) {
            return;
        }

        std::optional<ParamValue> current;
        {
            std::lock_guard<std::mutex> lock(mu_);
            observers_[key].push_back(observer);
            auto it = values_.find(key);
            if (it != values_.end()) {
                current = it->second;
            }
        }

        // 如果当前值可用，则立即回调一次（有助于确定性的启动/初始化）。
        if (current.has_value()) {
            observer->onParamChanged(key, *current);
        }
    }

    void setSnapshotPath(std::string path) {
        std::lock_guard<std::mutex> lock(mu_);
        snapshot_path_ = std::move(path);
    }

    void loadSnapshot() {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(mu_);
            path = snapshot_path_;
        }
        if (path.empty()) {
            return;
        }

        auto kvs = readKeyValueFile(path);
        if (kvs.empty()) {
            return;
        }

        // 仅对已声明的 key 应用快照，并进行类型解析。
        std::vector<std::pair<std::string, ParamValue>> to_notify;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (const auto& [k, raw] : kvs) {
                auto desc_it = descs_.find(k);
                if (desc_it == descs_.end()) {
                    continue;
                }
                auto parsed = parseFromString(raw, desc_it->second.type);
                if (!parsed.has_value() || !valueMatchesType(*parsed, desc_it->second.type)) {
                    continue;
                }
                values_[k] = *parsed;
                ParamStore::instance().set(k, toString(*parsed));
                to_notify.emplace_back(k, *parsed);
            }
        }

        for (const auto& [k, v] : to_notify) {
            std::vector<IParamObserver*> observers;
            {
                std::lock_guard<std::mutex> lock(mu_);
                auto it = observers_.find(k);
                if (it != observers_.end()) {
                    observers = it->second;
                }
            }
            for (auto* obs : observers) {
                if (obs != nullptr) {
                    obs->onParamChanged(k, v);
                }
            }
        }
    }

    void saveSnapshot() const {
        std::string path;
        std::unordered_map<std::string, std::string> kvs;
        {
            std::lock_guard<std::mutex> lock(mu_);
            path = snapshot_path_;
            if (path.empty()) {
                return;
            }
            kvs.reserve(values_.size());
            for (const auto& [k, v] : values_) {
                kvs.emplace(k, toString(v));
            }
        }
        writeKeyValueFile(path, kvs);
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, ParamDesc> descs_;
    std::unordered_map<std::string, ParamValue> values_;
    std::unordered_map<std::string, std::vector<IParamObserver*>> observers_;
    std::string snapshot_path_;
};

ParamServer::ParamServer() : impl_(std::make_unique<Impl>()) {}
ParamServer::~ParamServer() = default;

ParamServer::ParamServer(ParamServer&&) noexcept = default;
ParamServer& ParamServer::operator=(ParamServer&&) noexcept = default;

bool ParamServer::declare(const ParamDesc& desc) {
    return impl_->declareParam(desc);
}

std::optional<ParamValue> ParamServer::get(const std::string& key) const {
    return impl_->getValue(key);
}

bool ParamServer::set(const std::string& key, const ParamValue& value) {
    return impl_->setValue(key, value);
}

void ParamServer::subscribe(const std::string& key, IParamObserver* observer) {
    impl_->subscribeKey(key, observer);
}

void ParamServer::setSnapshotPath(std::string path) {
    impl_->setSnapshotPath(std::move(path));
}

void ParamServer::loadSnapshot() {
    impl_->loadSnapshot();
}

void ParamServer::saveSnapshot() const {
    impl_->saveSnapshot();
}

} // namespace wxz::core
