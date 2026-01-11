#pragma once

#include <string>

// 轻量生命周期抽象：方案层可实现 start/stop/状态查询；核心不落业务细节。
namespace wxz {

enum class LifecycleState {
    kUnknown,
    kInactive,
    kActivating,
    kActive,
    kDeactivating,
    kError
};

class ILifecycle {
public:
    virtual ~ILifecycle() = default;
    virtual bool configure() { return true; }
    virtual bool activate() { return true; }
    virtual bool deactivate() { return true; }
    virtual void shutdown() {}
    virtual LifecycleState state() const { return LifecycleState::kUnknown; }
    virtual const std::string& name() const = 0;
};

} // namespace wxz
