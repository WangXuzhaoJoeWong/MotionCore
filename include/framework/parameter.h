#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "param_server.h"
#include "strand.h"

#include "framework/status.h"

namespace wxz::framework {

/// ROS2-like 参数封装：
/// - 底层复用 MotionCore 的 IParamServer/ParamServer（线程安全，支持分布式实现）。
/// - 变更回调默认投递到指定 Strand，确保“不在 DDS listener 线程跑业务”。
class Parameters {
public:
    using Value = wxz::core::ParamValue;
    using Desc = wxz::core::ParamDesc;

    using OnChanged = std::function<void(const std::string& key, const Value& value)>;

    /// 参数变更观察者：用于桥接 IParamObserver -> Strand。
    class Observer final : public wxz::core::IParamObserver {
    public:
        Observer(wxz::core::Strand& strand, OnChanged cb)
            : strand_(strand), cb_(std::move(cb)) {}

        void onParamChanged(const std::string& key, const Value& value) override {
            // 注意：key/value 可能来自别的线程；这里统一投递到 strand。
            // Value 是 variant，可拷贝。
            (void)strand_.post([cb = cb_, key, value] {
                cb(key, value);
            });
        }

    private:
        wxz::core::Strand& strand_;
        OnChanged cb_;
    };

    Parameters() = default;

    explicit Parameters(std::shared_ptr<wxz::core::IParamServer> server)
        : server_(std::move(server)) {}

    /// 获取底层 server（便于与已有 param_server 生态互通）。
    std::shared_ptr<wxz::core::IParamServer> server() const { return server_; }

    /// 如果未显式注入 server，默认创建进程内 ParamServer。
    void ensure_server() {
        if (!server_) server_ = std::make_shared<wxz::core::ParamServer>();
    }

    /// 声明参数（ROS2 风格：建议先 declare 再 get/set）。
    bool declare(const Desc& desc) {
        ensure_server();
        return server_->declare(desc);
    }

    /// 便捷 declare：不强行要求 schema/type 字段由调用方填。
    bool declare(std::string name,
                 Value default_value,
                 std::string schema = {},
                 bool read_only = false) {
        Desc d;
        d.name = std::move(name);
        d.default_value = std::move(default_value);
        d.schema = std::move(schema);
        d.read_only = read_only;
        // type 字段由 ParamServer 内部根据 default_value 推断也可；这里留空。
        return declare(d);
    }

    std::optional<Value> get(const std::string& key) const {
        if (!server_) return std::nullopt;
        return server_->get(key);
    }

    /// 设置参数。
    /// - 成功返回 Status.ok=true。
    /// - 失败返回 Status.ok=false（原因当前只能给出 set_failed）。
    Status set(const std::string& key, const Value& value) {
        ensure_server();
        const bool ok = server_->set(key, value);
        if (ok) return Status::ok_status();
        Status st;
        st.ok = false;
        st.err_code = 1;
        st.err = "set_failed";
        return st;
    }

    /// 订阅参数变更：回调投递到 strand。
    /// - 注意：ParamServer 的 subscribe 是“按 key 订阅”；重复订阅同一个 key 会覆盖本封装的 observer。
    void on_changed(const std::string& key, wxz::core::Strand& strand, OnChanged cb) {
        ensure_server();
        auto obs = std::make_unique<Observer>(strand, std::move(cb));
        server_->subscribe(key, obs.get());
        std::lock_guard<std::mutex> lock(mu_);
        observers_[key] = std::move(obs);
    }

private:
    std::shared_ptr<wxz::core::IParamServer> server_;

    // 持有 observer 生命周期。
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<Observer>> observers_;
};

} // namespace wxz::framework
