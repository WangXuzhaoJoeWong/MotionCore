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

class FastddsChannel {
public:
    using Handler = std::function<void(const std::uint8_t* data, std::size_t size)>;

    struct HandlerEntry {
        std::uint64_t id{0};
        void* owner{nullptr};
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

    // 带作用域的订阅（可显式取消）。
    // owner 为可选 tag（例如插件实例指针），用于批量清理。
    Subscription subscribe_scoped(Handler handler, void* owner = nullptr);

    // 批量取消：移除所有带指定 owner tag 的 handler。
    void unsubscribe_owner(void* owner);

    void stop();

    // 可观测性
    std::uint64_t publish_success() const { return publish_success_.load(); }
    std::uint64_t publish_fail() const { return publish_fail_.load(); }
    std::uint64_t last_publish_duration_ns() const { return last_publish_duration_ns_.load(); }
    std::uint64_t messages_received() const { return messages_received_.load(); }

    // 暴露 writer 以便诊断（matched count 等）。调用方不可在 channel 生命周期之外持有该指针。
    eprosima::fastdds::dds::DataWriter* data_writer() const { return writer_; }

private:
    void apply_qos(const ChannelQoS& qos,
                   eprosima::fastdds::dds::DataWriterQos& wqos,
                   eprosima::fastdds::dds::DataReaderQos& rqos,
                   std::size_t history_depth);

    void cleanup();

    int domain_id_{0};
    std::string topic_name_;
    std::size_t max_payload_{0};

    eprosima::fastdds::dds::DomainParticipant* participant_{nullptr};
    eprosima::fastdds::dds::Publisher* publisher_{nullptr};
    eprosima::fastdds::dds::Subscriber* subscriber_{nullptr};
    eprosima::fastdds::dds::Topic* topic_{nullptr};
    eprosima::fastdds::dds::DataWriter* writer_{nullptr};
    eprosima::fastdds::dds::DataReader* reader_{nullptr};
    eprosima::fastdds::dds::TypeSupport type_;

    std::vector<HandlerEntry> handlers_;
    std::uint64_t next_handler_id_{1};
    std::mutex handler_mutex_;
    std::unique_ptr<eprosima::fastdds::dds::DataReaderListener> listener_;

    std::atomic<std::uint64_t> publish_success_{0};
    std::atomic<std::uint64_t> publish_fail_{0};
    std::atomic<std::uint64_t> last_publish_duration_ns_{0};
    std::atomic<std::uint64_t> messages_received_{0};

    // 析构安全：避免在 listener 回调执行期间删除 FastDDS 实体。
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint32_t> callbacks_inflight_{0};
};

} // namespace wxz::core
