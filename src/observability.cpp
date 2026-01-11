#include "observability.h"

#include <atomic>

namespace wxz::core {

namespace {
class NoopTraceHook final : public TraceHook {
public:
    void event(std::string_view, std::initializer_list<LabelView>) noexcept override {}
};

class NoopMetricsSink final : public MetricsSink {
public:
    void counter_add(std::string_view, double, std::initializer_list<LabelView>) noexcept override {}
    void gauge_set(std::string_view, double, std::initializer_list<LabelView>) noexcept override {}
    void histogram_observe(std::string_view, double, std::initializer_list<LabelView>) noexcept override {}
};

NoopTraceHook g_noop_trace;
NoopMetricsSink g_noop_metrics;

std::atomic<TraceHook*> g_trace_hook{&g_noop_trace};
std::atomic<MetricsSink*> g_metrics_sink{&g_noop_metrics};
} // namespace

void set_trace_hook(TraceHook* hook) noexcept {
    g_trace_hook.store(hook ? hook : &g_noop_trace, std::memory_order_release);
}

void set_metrics_sink(MetricsSink* sink) noexcept {
    g_metrics_sink.store(sink ? sink : &g_noop_metrics, std::memory_order_release);
}

TraceHook& trace() noexcept {
    return *g_trace_hook.load(std::memory_order_acquire);
}

MetricsSink& metrics() noexcept {
    return *g_metrics_sink.load(std::memory_order_acquire);
}

bool has_trace_hook() noexcept {
    return g_trace_hook.load(std::memory_order_acquire) != &g_noop_trace;
}

bool has_metrics_sink() noexcept {
    return g_metrics_sink.load(std::memory_order_acquire) != &g_noop_metrics;
}

} // namespace wxz::core
