#include "time_sync.h"

#include "observability.h"

#include <atomic>
#include <cstring>

#if defined(__linux__)
#include <sys/timex.h>
#endif

namespace wxz::core {

namespace {

class NoopTimeSyncProbe final : public TimeSyncProbe {
public:
    TimeSyncStatus probe() noexcept override {
        TimeSyncStatus st;
        st.synced = false;
        st.source = "noop";
        return st;
    }

    std::string_view name() const noexcept override { return "noop"; }
};

#if defined(__linux__)
class LinuxAdjtimexProbe final : public TimeSyncProbe {
public:
    TimeSyncStatus probe() noexcept override {
        TimeSyncStatus st;
        st.source = "adjtimex";

        struct timex tx;
        std::memset(&tx, 0, sizeof(tx));
        const int rc = ::adjtimex(&tx);

        st.raw_state = rc;
        st.raw_status = static_cast<std::uint32_t>(tx.status);
        st.maxerror_us = static_cast<std::int64_t>(tx.maxerror);
        st.esterror_us = static_cast<std::int64_t>(tx.esterror);

        // STA_UNSYNC 表示“未同步”。若未置位，则认为已同步（至少内核认为 NTP disciplined）。
        st.synced = (tx.status & STA_UNSYNC) == 0;
        return st;
    }

    std::string_view name() const noexcept override { return "adjtimex"; }
};
#endif

NoopTimeSyncProbe g_noop_probe;
#if defined(__linux__)
LinuxAdjtimexProbe g_linux_adjtimex_probe;
#endif

TimeSyncProbe* default_probe() {
#if defined(__linux__)
    return &g_linux_adjtimex_probe;
#else
    return &g_noop_probe;
#endif
}

std::atomic<TimeSyncProbe*> g_probe{default_probe()};

} // namespace

void set_timesync_probe(TimeSyncProbe* probe) noexcept {
    g_probe.store(probe ? probe : default_probe(), std::memory_order_release);
}

TimeSyncProbe& timesync_probe() noexcept {
    return *g_probe.load(std::memory_order_acquire);
}

TimeSyncStatus probe_timesync() noexcept {
    return timesync_probe().probe();
}

void publish_timesync_metrics(const TimeSyncStatus& st, std::string_view scope) noexcept {
    if (!has_metrics_sink()) return;

    // scope 仅用于区分多实例（例如不同 node_container）；为空时不加 label。
    if (scope.empty()) {
        metrics().gauge_set("wxz.timesync.synced", st.synced ? 1.0 : 0.0, {{"source", st.source}});
        metrics().gauge_set("wxz.timesync.maxerror_us", static_cast<double>(st.maxerror_us), {{"source", st.source}});
        metrics().gauge_set("wxz.timesync.esterror_us", static_cast<double>(st.esterror_us), {{"source", st.source}});
    } else {
        metrics().gauge_set("wxz.timesync.synced", st.synced ? 1.0 : 0.0, {{"source", st.source}, {"scope", scope}});
        metrics().gauge_set("wxz.timesync.maxerror_us", static_cast<double>(st.maxerror_us), {{"source", st.source}, {"scope", scope}});
        metrics().gauge_set("wxz.timesync.esterror_us", static_cast<double>(st.esterror_us), {{"source", st.source}, {"scope", scope}});
    }
}

} // namespace wxz::core
