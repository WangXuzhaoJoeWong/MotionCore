#pragma once
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace wxz {

// 一个小型的工作线程组，用于管理 N 个工作线程。
// 每个工作线程运行传入的可调用对象：`void(std::atomic<bool>& stop, int worker_id)`
class WorkerGroup {
public:
    WorkerGroup() = default;
    ~WorkerGroup();

    // 启动 N 个工作线程并运行 'fn'。如果已经在运行则返回 false。
    bool start(size_t n, std::function<void(std::atomic<bool>&, int)> fn);

    // 停止并 join 所有工作线程。
    void stop();

    // 查询是否正在运行
    bool running() const { return running_.load(); }

    // 当前工作线程数量
    size_t size() const { return threads_.size(); }

private:
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
    std::mutex mtx_;
    // 工作线程共享的停止标志。在 start() 时创建，在 stop() 时设置为 true。
    std::shared_ptr<std::atomic<bool>> stop_flag_{nullptr};
};

} // namespace wxz
