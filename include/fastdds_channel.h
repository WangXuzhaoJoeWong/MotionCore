#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>

#include "subscription.h"

#include "inproc_channel.h" // for ChannelQoS

#include "byte_buffer_pool.h"

// FastDDS 的 TypeSupport 持有 TopicDataType 实例。
#include <fastdds/dds/topic/TypeSupport.hpp>

namespace eprosima {
namespace fastdds {
namespace dds {
class DomainParticipant;
class Publisher;
class Subscriber;
class Topic;
class DataWriter;
class DataReader;
class DataReaderListener;
class TopicDataType;
struct DataWriterQos;
struct DataReaderQos;
} // namespace dds
} // namespace fastdds
} // namespace eprosima

namespace wxz::core {

class Executor;
class Strand;

class FastddsChannel {
public:
    using Handler = std::function<void(const std::uint8_t* data, std::size_t size)>;
    using LeasedHandler = std::function<void(ByteBufferLease&& msg)>;

    struct HandlerEntry {
        std::uint64_t id{0};
        void* owner{nullptr};
        Executor* executor{nullptr};
        Strand* strand{nullptr};
        Handler handler;
    };

    // domain_id：DDS domain；topic：topic 名；type_name 可选（默认使用内部 raw type）。
    FastddsChannel(int domain_id, std::string topic, const ChannelQoS& qos, std::size_t max_payload = 4096);

    // 高级用法：创建仅发布或仅订阅的 channel，以避免自订阅。
    // - enable_pub：创建 Publisher + DataWriter
    // - enable_sub：创建 Subscriber + DataReader
    FastddsChannel(int domain_id,
                  std::string topic,
                  const ChannelQoS& qos,
                  std::size_t max_payload,
                  bool enable_pub,
                  bool enable_sub);
    ~FastddsChannel();

    bool publish(const std::uint8_t* data, std::size_t size);
    void subscribe(Handler handler);

    // 类 ROS2 约定：不要在 FastDDS 的回调线程里直接调用用户 handler。
    // 这里会先拷贝消息字节，然后把回调投递到指定的调度器上执行。
    // 注意：如果希望接收侧做到“每条消息不分配内存”，优先使用 subscribe_leased_on()。
    void subscribe_on(Executor& ex, Handler handler);
    void subscribe_on(Strand& strand, Handler handler);

    // 使用可复用的缓冲池进行订阅。
    // 语义：
    // - FastDDS 回调线程把字节拷贝到池化 buffer 中（避免每条消息一次分配）。
    // - Handler 以 move-only 的 lease 形式拿到数据；lease 析构时 buffer 自动归还到池里。
    // - 如果缓冲池耗尽，则不会调用 leased handler。
    // 注意：每个 channel 只支持一个 leased handler。
    void subscribe_leased(ByteBufferPool& pool, LeasedHandler handler);

    // 类 ROS2 的 leased subscribe：
    // - FastDDS 回调线程先拷贝到池化 buffer。
    // - 然后把 lease 投递到指定调度器上执行。
    // - 如果缓冲池耗尽，则丢弃该回调（并计数）。
    void subscribe_leased_on(ByteBufferPool& pool, Executor& ex, LeasedHandler handler);
    void subscribe_leased_on(ByteBufferPool& pool, Strand& strand, LeasedHandler handler);

    // 带作用域的订阅（可显式取消）。
    // owner 为可选 tag（例如插件实例指针），用于批量清理。
    Subscription subscribe_scoped(Handler handler, void* owner = nullptr);

    // 类 ROS2 的“带调度投递”的 scoped subscribe。
    Subscription subscribe_scoped_on(Executor& ex, Handler handler, void* owner = nullptr);
    Subscription subscribe_scoped_on(Strand& strand, Handler handler, void* owner = nullptr);

    // 批量取消：移除所有带指定 owner tag 的 handler。
    void unsubscribe_owner(void* owner);

    void stop();

    // 可观测性
    std::uint64_t publish_success() const { return publish_success_.load(); }
    std::uint64_t publish_fail() const { return publish_fail_.load(); }
    std::uint64_t last_publish_duration_ns() const { return last_publish_duration_ns_.load(); }
    std::uint64_t messages_received() const { return messages_received_.load(); }

    // 丢弃统计（Drops）
    // - drop_pool_exhausted：请求 leased subscribe，但池里无可用 buffer。
    // - drop_dispatch_rejected：投递目标拒绝（executor/strand 已停止或队列已满）。
    std::uint64_t recv_drop_pool_exhausted() const { return recv_drop_pool_exhausted_.load(); }
    std::uint64_t recv_drop_dispatch_rejected() const { return recv_drop_dispatch_rejected_.load(); }

    // 暴露 writer 以便诊断（matched count 等）。调用方不可在 channel 生命周期之外持有该指针。
    eprosima::fastdds::dds::DataWriter* data_writer() const;

private:
    void apply_qos(const ChannelQoS& qos,
                   eprosima::fastdds::dds::DataWriterQos& wqos,
                   eprosima::fastdds::dds::DataReaderQos& rqos,
                   std::size_t history_depth);

    void cleanup();

    int domain_id_{0};
    std::string topic_name_;
    std::size_t max_payload_{0};

    // 在 publish/teardown 期间保护 DDS 实体指针。
    // 没有该锁的话，可能出现：定时线程还在 publish()，同时 cleanup() 删除 writer/participant。
    mutable std::mutex entity_mutex_;

    // 构造是否成功（用于选择 teardown 策略）。
    bool constructed_ok_{false};

    eprosima::fastdds::dds::DomainParticipant* participant_{nullptr};
    eprosima::fastdds::dds::Publisher* publisher_{nullptr};
    eprosima::fastdds::dds::Subscriber* subscriber_{nullptr};
    eprosima::fastdds::dds::Topic* topic_{nullptr};
    eprosima::fastdds::dds::DataWriter* writer_{nullptr};
    eprosima::fastdds::dds::DataReader* reader_{nullptr};
    eprosima::fastdds::dds::TypeSupport type_;

    std::vector<HandlerEntry> handlers_;
    ByteBufferPool* leased_pool_{nullptr};
    LeasedHandler leased_handler_;
    Executor* leased_executor_{nullptr};
    Strand* leased_strand_{nullptr};
    std::uint64_t next_handler_id_{1};
    std::mutex handler_mutex_;
    std::unique_ptr<eprosima::fastdds::dds::DataReaderListener> listener_;

    std::atomic<std::uint64_t> publish_success_{0};
    std::atomic<std::uint64_t> publish_fail_{0};
    std::atomic<std::uint64_t> last_publish_duration_ns_{0};
    std::atomic<std::uint64_t> messages_received_{0};

    std::atomic<std::uint64_t> recv_drop_pool_exhausted_{0};
    std::atomic<std::uint64_t> recv_drop_dispatch_rejected_{0};

    // 析构安全：避免在 listener 回调执行期间删除 FastDDS 实体。
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint32_t> callbacks_inflight_{0};
};

} // namespace wxz::core
