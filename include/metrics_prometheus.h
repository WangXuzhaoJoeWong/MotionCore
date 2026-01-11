#pragma once

#include "observability.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wxz::core {

// 一个小型的进程内 metrics sink，可渲染 Prometheus 文本格式（exposition）。
//
// 说明：
// - Histogram 渲染为最小实现：仅暴露 <name>_count 与 <name>_sum。
// - 适用于小/中等规模的进程内指标，不适合高基数场景。
class PrometheusMetricsSink final : public MetricsSink {
public:
    PrometheusMetricsSink() = default;
    ~PrometheusMetricsSink() override = default;

    PrometheusMetricsSink(const PrometheusMetricsSink&) = delete;
    PrometheusMetricsSink& operator=(const PrometheusMetricsSink&) = delete;

    void counter_add(std::string_view name, double value, std::initializer_list<LabelView> labels) noexcept override;
    void gauge_set(std::string_view name, double value, std::initializer_list<LabelView> labels) noexcept override;
    void histogram_observe(std::string_view name, double value, std::initializer_list<LabelView> labels) noexcept override;

    // 将当前已出现的所有指标渲染为 Prometheus 文本格式。
    std::string render() const;

private:
    enum class Type : std::uint8_t { Counter, Gauge, Histogram };

    struct Key {
        std::string name;
        std::string labels;

        bool operator==(const Key& o) const { return name == o.name && labels == o.labels; }
    };

    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            return std::hash<std::string>{}(k.name) ^ (std::hash<std::string>{}(k.labels) << 1);
        }
    };

    struct HistogramState {
        std::uint64_t count{0};
        double sum{0.0};
    };

    static std::string sanitize_metric_name_(std::string_view in);
    static std::string render_labels_(std::initializer_list<LabelView> labels);
    static std::string escape_label_value_(std::string_view v);

    mutable std::mutex mutex_;

    // 指标 family 类型（按 sanitize 后的 name 归类）。
    std::unordered_map<std::string, Type> types_;

    std::unordered_map<Key, double, KeyHash> counters_;
    std::unordered_map<Key, double, KeyHash> gauges_;
    std::unordered_map<Key, HistogramState, KeyHash> histograms_;
};

} // namespace wxz::core
