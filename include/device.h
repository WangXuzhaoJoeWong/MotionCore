#pragma once

#include <memory>
#include <string>
#include <vector>

namespace wxz::core {

struct DeviceHealth {
    bool ok{true};
    std::string message;
};

class IDevice {
public:
    virtual ~IDevice() = default;
    virtual std::string name() const = 0;
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual DeviceHealth health() const = 0;
};

class DeviceManager {
public:
    virtual ~DeviceManager() = default;
    virtual bool registerDevice(std::shared_ptr<IDevice> dev) = 0;
    virtual std::shared_ptr<IDevice> get(const std::string& name) const = 0;
    virtual std::vector<std::string> list() const = 0;
};

} // namespace wxz::core
