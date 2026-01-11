#include "metrics_prometheus.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace wxz::core {

namespace {
std::string escape_label_value(std::string_view v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '"':
                out += "\\\"";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string labels_to_string(std::initializer_list<LabelView> labels) {
    if (labels.size() == 0) return {};

    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto& kv : labels) {
        if (kv.key.empty()) continue;
        if (!first) out << ',';
        first = false;
        out << kv.key << "=\"" << escape_label_value(kv.value) << "\"";
    }
    out << '}';
    const auto s = out.str();
    // If all labels were empty keys, produce empty.
    return (s == "{}") ? std::string{} : s;
}

} // namespace

std::string PrometheusMetricsSink::sanitize_metric_name_(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "wxz_metric";
    // Prometheus names should not start with digit.
    if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
    return out;
}

std::string PrometheusMetricsSink::escape_label_value_(std::string_view v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '"':
                out += "\\\"";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string PrometheusMetricsSink::render_labels_(std::initializer_list<LabelView> labels) {
    return labels_to_string(labels);
}

void PrometheusMetricsSink::counter_add(std::string_view name, double value, std::initializer_list<LabelView> labels) noexcept {
    try {
        const auto n = sanitize_metric_name_(name);
        const auto l = labels_to_string(labels);
        std::lock_guard<std::mutex> lock(mutex_);
        types_.emplace(n, Type::Counter);
        counters_[Key{n, l}] += value;
    } catch (...) {
    }
}

void PrometheusMetricsSink::gauge_set(std::string_view name, double value, std::initializer_list<LabelView> labels) noexcept {
    try {
        const auto n = sanitize_metric_name_(name);
        const auto l = labels_to_string(labels);
        std::lock_guard<std::mutex> lock(mutex_);
        types_.emplace(n, Type::Gauge);
        gauges_[Key{n, l}] = value;
    } catch (...) {
    }
}

void PrometheusMetricsSink::histogram_observe(std::string_view name, double value, std::initializer_list<LabelView> labels) noexcept {
    try {
        const auto n = sanitize_metric_name_(name);
        const auto l = labels_to_string(labels);
        std::lock_guard<std::mutex> lock(mutex_);
        types_.emplace(n, Type::Histogram);
        auto& st = histograms_[Key{n, l}];
        st.count += 1;
        st.sum += value;
    } catch (...) {
    }
}

std::string PrometheusMetricsSink::render() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Stable-ish output: sort keys by name then labels.
    std::vector<std::string> family_names;
    family_names.reserve(types_.size());
    for (const auto& kv : types_) family_names.push_back(kv.first);
    std::sort(family_names.begin(), family_names.end());

    std::ostringstream out;
    for (const auto& fam : family_names) {
        const auto it = types_.find(fam);
        if (it == types_.end()) continue;
        const Type t = it->second;
        const char* prom_type = "untyped";
        switch (t) {
            case Type::Counter:
                prom_type = "counter";
                break;
            case Type::Gauge:
                prom_type = "gauge";
                break;
            case Type::Histogram:
                prom_type = "histogram";
                break;
        }
        out << "# TYPE " << fam << ' ' << prom_type << "\n";

        if (t == Type::Counter) {
            for (const auto& kv : counters_) {
                if (kv.first.name != fam) continue;
                out << kv.first.name;
                if (!kv.first.labels.empty()) out << kv.first.labels;
                out << ' ' << kv.second << "\n";
            }
        } else if (t == Type::Gauge) {
            for (const auto& kv : gauges_) {
                if (kv.first.name != fam) continue;
                out << kv.first.name;
                if (!kv.first.labels.empty()) out << kv.first.labels;
                out << ' ' << kv.second << "\n";
            }
        } else if (t == Type::Histogram) {
            for (const auto& kv : histograms_) {
                if (kv.first.name != fam) continue;
                // Expose minimal Prometheus histogram fields.
                out << kv.first.name << "_count";
                if (!kv.first.labels.empty()) out << kv.first.labels;
                out << ' ' << kv.second.count << "\n";

                out << kv.first.name << "_sum";
                if (!kv.first.labels.empty()) out << kv.first.labels;
                out << ' ' << kv.second.sum << "\n";
            }
        }

        out << "\n";
    }

    return out.str();
}

} // namespace wxz::core
