#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore.h>
#include <string>
#include <thread>
#include <vector>

#include "subscription.h"

namespace wxz::core {

// 同机共享内存通道（偏 SPSC 场景），使用命名 POSIX shm + 命名信号量进行通知。
// 每个 slot 存储 length(uint32) + payload 字节；写端每条消息 post 一次信号量。
class ShmChannel {
public:
    using Handler = std::function<void(const std::uint8_t* data, std::size_t size)>;

    // name：POSIX shm 对象名（会确保前缀 '/';）。
    // capacity：slot 数量（建议为 2 的幂）。
    // slot_size：每个 slot 的字节数（可用 payload 字节 = slot_size - sizeof(uint32_t)）。
    // create：true 表示创建/截断并初始化区域；false 表示附加到已有区域。
    ShmChannel(std::string name, std::size_t capacity, std::size_t slot_size, bool create);
    ~ShmChannel();

    bool publish(const std::uint8_t* data, std::size_t size);

    void subscribe(Handler handler);

    // 带作用域的订阅（可显式取消）。
    // owner 为可选 tag（例如插件实例指针），用于批量清理。
    Subscription subscribe_scoped(Handler handler, void* owner = nullptr);

    // 批量取消：移除所有带指定 owner tag 的 handler。
    void unsubscribe_owner(void* owner);

    void stop();

    // 可观测性
    std::uint64_t publish_success() const { return publish_success_.load(); }
    std::uint64_t publish_fail() const { return publish_fail_.load(); }
    std::uint64_t messages_delivered() const { return messages_delivered_.load(); }

private:
    struct Header {
        std::atomic<std::uint32_t> head;
        std::atomic<std::uint32_t> tail;
        std::uint32_t capacity;
        std::uint32_t slot_size;
        std::uint32_t magic;
    };

    struct SlotView {
        std::uint8_t* ptr{nullptr};
        std::size_t bytes{0};
    };

    SlotView slot(std::uint32_t idx) const;
    void dispatch_loop();
    static std::string normalize_name(const std::string& n);
    static std::string sem_name_from(const std::string& n);

    std::string name_;
    std::string sem_name_;
    int shm_fd_{-1};
    sem_t* sem_{nullptr};
    Header* hdr_{nullptr};
    std::uint8_t* base_{nullptr};
    std::size_t region_bytes_{0};
    bool owner_{false};

    struct HandlerEntry {
        std::uint64_t id{0};
        void* owner{nullptr};
        Handler handler;
    };

    std::vector<HandlerEntry> handlers_;
    std::uint64_t next_handler_id_{1};
    std::mutex handler_mutex_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    std::atomic<std::uint64_t> publish_success_{0};
    std::atomic<std::uint64_t> publish_fail_{0};
    std::atomic<std::uint64_t> messages_delivered_{0};
};

} // namespace wxz::core
