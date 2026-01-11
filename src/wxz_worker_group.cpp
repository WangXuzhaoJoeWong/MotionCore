#include "internal/wxz_worker_group.h"

namespace wxz {

WorkerGroup::~WorkerGroup() {
    stop();
}

bool WorkerGroup::start(size_t n, std::function<void(std::atomic<bool>&, int)> fn) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (running_.load()) return false;

    stop_flag_ = std::make_shared<std::atomic<bool>>(false);
    threads_.clear();
    threads_.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        // 捕获停止标志和函数的副本并在独立线程中执行
        threads_.emplace_back([stop_flag = stop_flag_, fn, i]() {
            try {
                fn(*stop_flag, static_cast<int>(i));
            } catch (...) {
                // 捕获所有异常以避免程序终止；实际项目应记录或上报日志以便排查
            }
        });
    }

    running_.store(true);
    return true;
}

void WorkerGroup::stop() {
    std::unique_lock<std::mutex> lk(mtx_);
    if (!running_.load()) return;

    if (stop_flag_) stop_flag_->store(true);
    lk.unlock();

    for (auto &t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }

    threads_.clear();
    stop_flag_.reset();
    running_.store(false);
}

} // namespace wxz
