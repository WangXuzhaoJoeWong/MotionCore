#include "param_server.h"

#include "internal/param_server.h"
#include "internal/param_store.h"
#include "service_common.h"

#include <chrono>
#include <mutex>
#include <string>
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

} // namespace

class DistributedParamServer::Impl {
public:
    Impl(std::string set_topic, std::string ack_topic)
        : set_topic_(std::move(set_topic)), ack_topic_(std::move(ack_topic)) {
        const int domain_id = wxz::core::getenv_int("WXZ_DOMAIN_ID", 0);
        internal_ = std::make_unique<wxz::core::internal::ParamServer>(domain_id, set_topic_, ack_topic_);
        internal_->start();
    }

    ~Impl() {
        if (internal_) {
            internal_->stop();
        }
    }

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

        const std::string key = desc.name;
        const std::string type = desc.type;
        const bool read_only = desc.read_only;
        const std::string default_str = toString(desc.default_value);

        {
            std::lock_guard<std::mutex> lock(mu_);
            types_[key] = type;
        }

        // 保持内部 schema 同步（wire-level 校验）。
        wxz::core::internal::ParamServer::ParamSpec spec;
        spec.type = type;
        spec.read_only = read_only;
        internal_->setSchema(key, spec);

        // 用回调声明，确保远端更新可被观察到。
        internal_->declare(
            key,
            default_str,
            [this, key, type](const std::string&, const std::string& val) {
                auto parsed = parseFromString(val, type);
                if (!parsed.has_value()) {
                    return;
                }
                notify(key, *parsed);
            });

        // Apply default immediately to ensure ParamStore + observers are consistent.
        internal_->applyBulk({{key, default_str}});
        notify(key, desc.default_value);
        return true;
    }

    std::optional<ParamValue> getValue(const std::string& key) const {
        // 优先使用缓存的强类型值。
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = values_.find(key);
            if (it != values_.end()) {
                return it->second;
            }
        }

        // 回退：从 ParamStore 读取（字符串快照），若已知类型则进行解析。
        auto raw = ParamStore::instance().get(key);
        if (!raw.has_value()) {
            return std::nullopt;
        }

        std::string type = "string";
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = types_.find(key);
            if (it != types_.end()) {
                type = it->second;
            }
        }

        auto parsed = parseFromString(*raw, type);
        if (!parsed.has_value()) {
            return ParamValue{*raw};
        }
        return parsed;
    }

    bool setValue(const std::string& key, const ParamValue& value) {
        // 本地应用（非 wire），供需要进程内 API 的调用方使用。
        // 远端变更预期通过 param.set topic 进入。
        std::string type = "";
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = types_.find(key);
            if (it == types_.end()) {
                return false;
            }
            type = it->second;
        }

        if (!valueMatchesType(value, type)) {
            return false;
        }

        internal_->applyBulk({{key, toString(value)}});
        notify(key, value);
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

        if (!current.has_value()) {
            current = getValue(key);
        }
        if (current.has_value()) {
            observer->onParamChanged(key, *current);
        }
    }

    void setHttpFetchUrl(const std::string& url, std::chrono::milliseconds interval) {
        internal_->setHttpFetch(url, interval);
    }

    void setHttpFetchUrlList(const std::vector<std::string>& urls, std::chrono::milliseconds interval) {
        internal_->setHttpFetchList(urls, interval);
    }

    void setFetchCallback(DistributedParamServer::FetchCallback cb, std::chrono::milliseconds interval) {
        internal_->setFetchCallback(std::move(cb), interval);
    }

    void enableExportService(std::string request_topic, std::string reply_topic) {
        internal_->setExportTopics(std::move(request_topic), std::move(reply_topic));
    }

    void setSnapshotPath(std::string path) {
        internal_->setSnapshotPath(std::move(path));
    }

    void loadSnapshot() {
        internal_->loadSnapshot();
    }

    void saveSnapshot() {
        internal_->saveSnapshot();
    }

    bool hasEnteredLoop() const {
        if (!internal_) {
            return false;
        }
        return internal_->hasEnteredLoop();
    }

private:
    void notify(const std::string& key, const ParamValue& value) {
        std::vector<IParamObserver*> to_notify;
        {
            std::lock_guard<std::mutex> lock(mu_);
            values_[key] = value;
            auto it = observers_.find(key);
            if (it != observers_.end()) {
                to_notify = it->second;
            }
        }

        for (auto* obs : to_notify) {
            if (obs != nullptr) {
                obs->onParamChanged(key, value);
            }
        }
    }

private:
    std::string set_topic_;
    std::string ack_topic_;
    std::unique_ptr<wxz::core::internal::ParamServer> internal_;

    mutable std::mutex mu_;
    std::unordered_map<std::string, std::string> types_;
    std::unordered_map<std::string, ParamValue> values_;
    std::unordered_map<std::string, std::vector<IParamObserver*>> observers_;
};

DistributedParamServer::DistributedParamServer(std::string set_topic, std::string ack_topic)
    : impl_(std::make_unique<Impl>(std::move(set_topic), std::move(ack_topic))) {}

DistributedParamServer::~DistributedParamServer() = default;

DistributedParamServer::DistributedParamServer(DistributedParamServer&&) noexcept = default;
DistributedParamServer& DistributedParamServer::operator=(DistributedParamServer&&) noexcept = default;

bool DistributedParamServer::declare(const ParamDesc& desc) {
    return impl_->declareParam(desc);
}

std::optional<ParamValue> DistributedParamServer::get(const std::string& key) const {
    return impl_->getValue(key);
}

bool DistributedParamServer::set(const std::string& key, const ParamValue& value) {
    return impl_->setValue(key, value);
}

void DistributedParamServer::subscribe(const std::string& key, IParamObserver* observer) {
    impl_->subscribeKey(key, observer);
}

void DistributedParamServer::setHttpFetch(const std::string& url, std::chrono::milliseconds interval) {
    impl_->setHttpFetchUrl(url, interval);
}

void DistributedParamServer::setHttpFetchList(const std::vector<std::string>& urls, std::chrono::milliseconds interval) {
    impl_->setHttpFetchUrlList(urls, interval);
}

void DistributedParamServer::setFetchCallback(FetchCallback cb, std::chrono::milliseconds interval) {
    impl_->setFetchCallback(std::move(cb), interval);
}

void DistributedParamServer::enableExportService(std::string request_topic, std::string reply_topic) {
    impl_->enableExportService(std::move(request_topic), std::move(reply_topic));
}

void DistributedParamServer::setSnapshotPath(std::string path) {
    impl_->setSnapshotPath(std::move(path));
}

void DistributedParamServer::loadSnapshot() {
    impl_->loadSnapshot();
}

void DistributedParamServer::saveSnapshot() const {
    impl_->saveSnapshot();
}

bool DistributedParamServer::hasEnteredLoop() const {
    return impl_ ? impl_->hasEnteredLoop() : false;
}

} // namespace wxz::core
