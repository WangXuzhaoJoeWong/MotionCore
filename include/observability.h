#pragma once

#include <initializer_list>
#include <string_view>

namespace wxz::core {

struct LabelView {
    std::string_view key;
    std::string_view value;
};

class TraceHook {
public:
    virtual ~TraceHook() = default;

    // 轻量结构化事件 hook：用于 trace-id 透传或 span 适配。
    // 实现应保证不抛异常。
    virtual void event(std::string_view name, std::initializer_list<LabelView> fields) noexcept = 0;
};

class MetricsSink {
public:
    virtual ~MetricsSink() = default;

    // 最小化 metrics 抽象。实现应保证不抛异常。
    virtual void counter_add(std::string_view name,
                             double value,
                             std::initializer_list<LabelView> labels) noexcept = 0;

    virtual void gauge_set(std::string_view name,
                           double value,
                           std::initializer_list<LabelView> labels) noexcept = 0;

    virtual void histogram_observe(std::string_view name,
                                   double value,
                                   std::initializer_list<LabelView> labels) noexcept = 0;
};

// 全局 hook（进程级）。所有权由调用方保留；hook 的生命周期必须覆盖其注册期。
void set_trace_hook(TraceHook* hook) noexcept;
void set_metrics_sink(MetricsSink* sink) noexcept;

TraceHook& trace() noexcept;
MetricsSink& metrics() noexcept;

bool has_trace_hook() noexcept;
bool has_metrics_sink() noexcept;

} // namespace wxz::core
