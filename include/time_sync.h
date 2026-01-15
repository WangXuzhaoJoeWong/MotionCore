#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace wxz::core {

// 时间同步健康状态（尽量与实现解耦：NTP/PTP/chrony/ptp4l 都可以适配）。
struct TimeSyncStatus {
    bool synced{false};

    // 探针来源："adjtimex"/"chrony"/"ptp4l"/"unknown"...
    std::string source{"unknown"};

    // 以下字段为可选诊断信息；未知时为 0。
    std::int64_t maxerror_us{0};
    std::int64_t esterror_us{0};

    // 原始状态位/状态码（与具体 probe 相关），仅用于排障。
    int raw_state{0};
    std::uint32_t raw_status{0};
};

class TimeSyncProbe {
public:
    virtual ~TimeSyncProbe() = default;
    virtual TimeSyncStatus probe() noexcept = 0;
    virtual std::string_view name() const noexcept = 0;
};

// 进程级全局 probe。所有权由调用方保留；对象生命周期必须覆盖其注册期。
// - set_timesync_probe(nullptr) 会恢复为默认 probe（Linux 下优先 adjtimex）。
void set_timesync_probe(TimeSyncProbe* probe) noexcept;
TimeSyncProbe& timesync_probe() noexcept;

// 主动探测一次。
TimeSyncStatus probe_timesync() noexcept;

// 输出最小 metrics（若未设置 MetricsSink 则 no-op）。
// - wxz.timesync.synced (gauge 0/1)
// - wxz.timesync.maxerror_us (gauge)
// - wxz.timesync.esterror_us (gauge)
void publish_timesync_metrics(const TimeSyncStatus& st, std::string_view scope = "") noexcept;

} // namespace wxz::core
