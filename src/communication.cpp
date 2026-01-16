#include "internal/legacy_communicator.h"
#include "dto/event_dto.h"

#include "fastdds_channel.h"
#include "internal/dds_security_precheck.h"
#include "internal/fastdds_participant_factory.h"
#include "internal/fastcdr_compat.h"
#include "service_common.h"

#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastdds/rtps/common/SerializedPayload.h>
#include <fastdds/rtps/common/InstanceHandle.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastdds/rtps/common/Time_t.h>

#ifdef MEMBER_ID_INVALID
#undef MEMBER_ID_INVALID
#endif

#include <fastcdr/FastBuffer.h>
#include <fastcdr/Cdr.h>
#include <map>
#include <memory>
#include <mutex>
#include <deque>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <algorithm>


namespace {
bool fallback_allowed() {
    const char* v = std::getenv("COMM_DISABLE_FALLBACK");
    return !(v && v[0] != '\0' && std::string(v) != "0");
}

void log_fallback(const char* path) {
    std::cerr << "[comm] fallback path used: " << path
              << " (set COMM_DISABLE_FALLBACK=1 to disable)" << std::endl;
}

std::string lower_copy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

wxz::core::ChannelQoS channel_qos_from_legacy(const TopicQosProfile& profile) {
    wxz::core::ChannelQoS qos;
    const std::string rel = lower_copy(profile.reliability);
    qos.reliability = (rel == "best_effort") ? wxz::core::ChannelQoS::Reliability::best_effort
                                            : wxz::core::ChannelQoS::Reliability::reliable;

    const std::string hist = lower_copy(profile.history);
    qos.history = (hist == "keep_all") ? 0 : static_cast<std::size_t>(std::max(1, profile.depth));

    const std::string dur = lower_copy(profile.durability);
    qos.durability = (dur == "transient_local") ? wxz::core::ChannelQoS::Durability::transient_local
                                                : wxz::core::ChannelQoS::Durability::volatile_kind;

    if (profile.deadline_ns.has_value() && *profile.deadline_ns > 0) {
        qos.deadline_ns = static_cast<std::uint64_t>(*profile.deadline_ns);
    }
    if (profile.latency_budget_ns.has_value() && *profile.latency_budget_ns > 0) {
        qos.latency_budget_ns = static_cast<std::uint64_t>(*profile.latency_budget_ns);
    }
    if (profile.lifespan_ns.has_value() && *profile.lifespan_ns > 0) {
        qos.lifespan_ns = static_cast<std::uint64_t>(*profile.lifespan_ns);
    }
    if (profile.time_based_filter_ns.has_value() && *profile.time_based_filter_ns > 0) {
        qos.time_based_filter_ns = static_cast<std::uint64_t>(*profile.time_based_filter_ns);
    }

    const std::string liv = lower_copy(profile.liveliness);
    qos.liveliness = (liv == "manual_by_topic") ? wxz::core::ChannelQoS::Liveliness::manual_by_topic
                                                : wxz::core::ChannelQoS::Liveliness::automatic;

    const std::string own = lower_copy(profile.ownership);
    qos.ownership = (own == "exclusive") ? wxz::core::ChannelQoS::Ownership::exclusive
                                        : wxz::core::ChannelQoS::Ownership::shared;
    qos.ownership_strength = profile.ownership_strength.value_or(0);
    qos.transport_priority = profile.transport_priority.value_or(0);
    qos.async_publish = profile.async_publish.value_or(false);
    return qos;
}

std::vector<std::uint8_t> serialize_event_dto_to_cdr_bytes(const EventDTO& dto) {
    // 类似于旧版 DDS EventDTO 类型：存储封装信息（encapsulation）+ 各字段。
    // 这可以在不同字节序之间稳健解码。
    const std::size_t estimate =
        4 +
        4 + dto.schema_id.size() +
        4 + dto.topic.size() +
        4 + dto.payload.size() +
        8 +
        4 + dto.event_id.size() +
        4 + dto.source.size() +
        32;

    std::vector<std::uint8_t> buf;
    buf.resize(estimate);

    eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(buf.data()), buf.size());
    eprosima::fastcdr::Cdr ser(fastbuffer);

    wxz::internal::fastcdr_compat::serialize_encapsulation(ser);
    wxz::internal::fastcdr_compat::write(ser, static_cast<uint32_t>(dto.version));
    wxz::internal::fastcdr_compat::write(ser, dto.schema_id);
    wxz::internal::fastcdr_compat::write(ser, dto.topic);
    wxz::internal::fastcdr_compat::write(ser, dto.payload);
    wxz::internal::fastcdr_compat::write(ser, dto.timestamp);
    wxz::internal::fastcdr_compat::write(ser, dto.event_id);
    wxz::internal::fastcdr_compat::write(ser, dto.source);

    const auto len = wxz::internal::fastcdr_compat::serialized_length_u32(ser);
    buf.resize(len);
    return buf;
}

bool deserialize_event_dto_from_cdr_bytes(const std::uint8_t* data, std::size_t size, EventDTO& out) {
    if (!data || size == 0) return false;
    // FastBuffer 需要可写缓冲区；这里拷贝一份用于安全解码。
    std::vector<std::uint8_t> tmp(data, data + size);
    eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(tmp.data()), tmp.size());
    eprosima::fastcdr::Cdr deser(fastbuffer);
    try {
        wxz::internal::fastcdr_compat::read_encapsulation(deser);
        uint32_t version = 0;
        wxz::internal::fastcdr_compat::read(deser, version);
        out.version = version;
        wxz::internal::fastcdr_compat::read(deser, out.schema_id);
        wxz::internal::fastcdr_compat::read(deser, out.topic);
        wxz::internal::fastcdr_compat::read(deser, out.payload);
        wxz::internal::fastcdr_compat::read(deser, out.timestamp);
        wxz::internal::fastcdr_compat::read(deser, out.event_id);
        wxz::internal::fastcdr_compat::read(deser, out.source);
        return true;
    } catch (...) {
        return false;
    }
}

inline eprosima::fastrtps::Duration_t duration_from_ns(int64_t ns) {
    using eprosima::fastrtps::Duration_t;
    if (ns < 0) return eprosima::fastrtps::c_TimeInfinite;
    int64_t sec = ns / 1000000000;
    int64_t nsec = ns % 1000000000;
    return Duration_t(static_cast<int32_t>(sec), static_cast<uint32_t>(nsec));
}

void apply_common_qos(const TopicQosProfile& profile, eprosima::fastdds::dds::DataWriterQos& qos) {
    using namespace eprosima::fastdds::dds;
    const std::string rel = lower_copy(profile.reliability);
    qos.reliability().kind = (rel == "best_effort") ? BEST_EFFORT_RELIABILITY_QOS : RELIABLE_RELIABILITY_QOS;

    const std::string hist = lower_copy(profile.history);
    qos.history().kind = (hist == "keep_all") ? KEEP_ALL_HISTORY_QOS : KEEP_LAST_HISTORY_QOS;
    if (qos.history().kind == KEEP_LAST_HISTORY_QOS && profile.depth > 0) {
        qos.history().depth = profile.depth;
    }

    const std::string dur = lower_copy(profile.durability);
    qos.durability().kind = (dur == "transient_local") ? TRANSIENT_LOCAL_DURABILITY_QOS : VOLATILE_DURABILITY_QOS;

    const std::string liv = lower_copy(profile.liveliness);
    qos.liveliness().kind = (liv == "manual_by_topic") ? MANUAL_BY_TOPIC_LIVELINESS_QOS : AUTOMATIC_LIVELINESS_QOS;
    if (profile.liveliness_lease_ns) {
        qos.liveliness().lease_duration = duration_from_ns(*profile.liveliness_lease_ns);
    }

    const std::string own = lower_copy(profile.ownership);
    qos.ownership().kind = (own == "exclusive") ? EXCLUSIVE_OWNERSHIP_QOS : SHARED_OWNERSHIP_QOS;
    if (profile.ownership_strength && qos.ownership().kind == EXCLUSIVE_OWNERSHIP_QOS) {
        qos.ownership_strength().value = static_cast<int32_t>(*profile.ownership_strength);
    }

    if (profile.deadline_ns) qos.deadline().period = duration_from_ns(*profile.deadline_ns);
    if (profile.latency_budget_ns) qos.latency_budget().duration = duration_from_ns(*profile.latency_budget_ns);
    if (profile.lifespan_ns) qos.lifespan().duration = duration_from_ns(*profile.lifespan_ns);
    if (profile.transport_priority) qos.transport_priority().value = *profile.transport_priority;
    if (profile.async_publish && *profile.async_publish) {
        qos.publish_mode().kind = ASYNCHRONOUS_PUBLISH_MODE;
    } else {
        qos.publish_mode().kind = SYNCHRONOUS_PUBLISH_MODE;
    }
}

void apply_common_qos(const TopicQosProfile& profile, eprosima::fastdds::dds::DataReaderQos& qos) {
    using namespace eprosima::fastdds::dds;
    const std::string rel = lower_copy(profile.reliability);
    qos.reliability().kind = (rel == "best_effort") ? BEST_EFFORT_RELIABILITY_QOS : RELIABLE_RELIABILITY_QOS;

    const std::string hist = lower_copy(profile.history);
    qos.history().kind = (hist == "keep_all") ? KEEP_ALL_HISTORY_QOS : KEEP_LAST_HISTORY_QOS;
    if (qos.history().kind == KEEP_LAST_HISTORY_QOS && profile.depth > 0) {
        qos.history().depth = profile.depth;
    }

    const std::string dur = lower_copy(profile.durability);
    qos.durability().kind = (dur == "transient_local") ? TRANSIENT_LOCAL_DURABILITY_QOS : VOLATILE_DURABILITY_QOS;

    const std::string liv = lower_copy(profile.liveliness);
    qos.liveliness().kind = (liv == "manual_by_topic") ? MANUAL_BY_TOPIC_LIVELINESS_QOS : AUTOMATIC_LIVELINESS_QOS;
    if (profile.liveliness_lease_ns) {
        qos.liveliness().lease_duration = duration_from_ns(*profile.liveliness_lease_ns);
    }

    const std::string own = lower_copy(profile.ownership);
    qos.ownership().kind = (own == "exclusive") ? EXCLUSIVE_OWNERSHIP_QOS : SHARED_OWNERSHIP_QOS;
    if (profile.deadline_ns) qos.deadline().period = duration_from_ns(*profile.deadline_ns);
    if (profile.latency_budget_ns) qos.latency_budget().duration = duration_from_ns(*profile.latency_budget_ns);
    if (profile.time_based_filter_ns) qos.time_based_filter().minimum_separation = duration_from_ns(*profile.time_based_filter_ns);
}

}

// 简单字符串类型的 TopicDataType 实现；保留作为字符串通道
class StringMsgType : public eprosima::fastdds::dds::TopicDataType {
public:
    StringMsgType() {
        setName("StringMsg");
        m_typeSize = 4 + 1024; // conservative buffer for small string payloads
        m_isGetKeyDefined = false;
    }
    bool serialize(void* data,
                   eprosima::fastrtps::rtps::SerializedPayload_t* payload) override {
        std::string* s = static_cast<std::string*>(data);
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->max_size);
        eprosima::fastcdr::Cdr ser(fastbuffer);
        wxz::internal::fastcdr_compat::write(ser, *s);
        payload->length = wxz::internal::fastcdr_compat::serialized_length_u32(ser);
        return true;
    }
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload, void* data) override {
        std::string* s = static_cast<std::string*>(data);
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->length);
        eprosima::fastcdr::Cdr deser(fastbuffer);
        wxz::internal::fastcdr_compat::read(deser, *s);
        return true;
    }
    std::function<uint32_t()> getSerializedSizeProvider(void* data) override {
        std::string* s = static_cast<std::string*>(data);
        return [s]() { return static_cast<uint32_t>(4 + s->size()); };
    }
    void* createData() override { return new std::string(); }
    void deleteData(void* data) override { delete static_cast<std::string*>(data); }
    bool getKey(void*, eprosima::fastrtps::rtps::InstanceHandle_t*, bool) override { return false; }
};

// EventDTO 的 TopicDataType；后续可替换为 IDL 生成
class EventDTOType : public eprosima::fastdds::dds::TopicDataType {
public:
    EventDTOType() {
        setName("EventDTO");
        // 保守估计大小：version + schema_id + topic + payload + timestamp + event_id + source
        m_typeSize = 4 + 4 + 1024 + 1024 + 4096 + 8 + 512 + 256;
        m_isGetKeyDefined = false;
    }
    bool serialize(void* data,
                   eprosima::fastrtps::rtps::SerializedPayload_t* payload) override {
        EventDTO* dto = static_cast<EventDTO*>(data);
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->max_size);
        eprosima::fastcdr::Cdr ser(fastbuffer);
        wxz::internal::fastcdr_compat::write(ser, dto->version);
        wxz::internal::fastcdr_compat::write(ser, dto->schema_id);
        wxz::internal::fastcdr_compat::write(ser, dto->topic);
        wxz::internal::fastcdr_compat::write(ser, dto->payload);
        wxz::internal::fastcdr_compat::write(ser, dto->timestamp);
        wxz::internal::fastcdr_compat::write(ser, dto->event_id);
        wxz::internal::fastcdr_compat::write(ser, dto->source);
        payload->length = wxz::internal::fastcdr_compat::serialized_length_u32(ser);
        return true;
    }
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload, void* data) override {
        EventDTO* dto = static_cast<EventDTO*>(data);
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->length);
        eprosima::fastcdr::Cdr deser(fastbuffer);
        wxz::internal::fastcdr_compat::read(deser, dto->version);
        wxz::internal::fastcdr_compat::read(deser, dto->schema_id);
        wxz::internal::fastcdr_compat::read(deser, dto->topic);
        wxz::internal::fastcdr_compat::read(deser, dto->payload);
        // 新增元数据字段；若发送端未设置则为默认值
        try { wxz::internal::fastcdr_compat::read(deser, dto->timestamp); } catch (...) { dto->timestamp = 0; }
        try { wxz::internal::fastcdr_compat::read(deser, dto->event_id); } catch (...) { dto->event_id.clear(); }
        try { wxz::internal::fastcdr_compat::read(deser, dto->source); } catch (...) { dto->source.clear(); }
        return true;
    }
    std::function<uint32_t()> getSerializedSizeProvider(void* data) override {
        EventDTO* dto = static_cast<EventDTO*>(data);
        return [dto]() {
            // 这里只做近似估计即可
            return static_cast<uint32_t>(
                4 +
                4 + dto->schema_id.size() +
                4 + dto->topic.size() +
                4 + dto->payload.size() +
                8 +
                4 + dto->event_id.size() +
                4 + dto->source.size());
        };
    }
    void* createData() override { return new EventDTO(); }
    void deleteData(void* data) override { delete static_cast<EventDTO*>(data); }
    bool getKey(void*, eprosima::fastrtps::rtps::InstanceHandle_t*, bool) override { return false; }
};

struct FastDDSCommunicator::Impl {
    Impl();
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void send(const std::string& topic, const std::string& message);
    std::string receive(const std::string& topic);
    void sendDTO(const std::string& topic, const EventDTO& dto);
    bool receiveDTO(const std::string& topic, EventDTO& out);

    void setPeers(const std::vector<std::string>& peers);
    void setTopicQos(const std::string& topic, const TopicQosProfile& qos);

private:
    struct ChannelTopicEntry {
        std::unique_ptr<wxz::core::FastddsChannel> channel;
        wxz::core::Subscription sub;
        std::mutex mu;
        std::deque<std::vector<std::uint8_t>> queue;
        std::size_t capacity{32};
    };

    ChannelTopicEntry& ensure_topic_channel(std::map<std::string, ChannelTopicEntry>& table, const std::string& topic);

    bool fallback_enabled_{false};

    int domain_id_{0};
    std::size_t max_payload_{65536};

    std::map<std::string, ChannelTopicEntry> topics_;
    std::map<std::string, ChannelTopicEntry> dto_topics_;
    std::map<std::string, TopicQosProfile> topic_qos_;
    std::vector<std::string> peers_;

    std::mutex local_mutex_;
    std::map<std::string, std::string> local_string_queue_;
    std::map<std::string, EventDTO> local_dto_queue_;
};

FastDDSCommunicator::Impl::Impl() {
    fallback_enabled_ = fallback_allowed();
    domain_id_ = wxz::core::getenv_int("WXZ_DOMAIN_ID", 0);
    // 默认把这个 legacy 适配器的上限放宽，避免大 payload 时出现意外丢弃。
    max_payload_ = static_cast<std::size_t>(std::max(4096, wxz::core::getenv_int("WXZ_LEGACY_COMM_MAX_PAYLOAD", 65536)));
}

FastDDSCommunicator::Impl::~Impl() {
    // 尽力而为：先停止订阅，避免回调与 teardown 竞争。
    for (auto& kv : topics_) {
        kv.second.sub.reset();
        if (kv.second.channel) kv.second.channel->stop();
    }
    for (auto& kv : dto_topics_) {
        kv.second.sub.reset();
        if (kv.second.channel) kv.second.channel->stop();
    }
}

FastDDSCommunicator::FastDDSCommunicator() : impl_(std::make_unique<Impl>()) {}

FastDDSCommunicator::~FastDDSCommunicator() = default;

void FastDDSCommunicator::send(const std::string& topic, const std::string& message) { impl_->send(topic, message); }
std::string FastDDSCommunicator::receive(const std::string& topic) { return impl_->receive(topic); }
void FastDDSCommunicator::sendDTO(const std::string& topic, const EventDTO& dto) { impl_->sendDTO(topic, dto); }
bool FastDDSCommunicator::receiveDTO(const std::string& topic, EventDTO& out) { return impl_->receiveDTO(topic, out); }
void FastDDSCommunicator::setPeers(const std::vector<std::string>& peers) { impl_->setPeers(peers); }
void FastDDSCommunicator::setTopicQos(const std::string& topic, const TopicQosProfile& qos) { impl_->setTopicQos(topic, qos); }

void FastDDSCommunicator::Impl::send(const std::string& topic, const std::string& message) {
    if (fallback_enabled_) {
        std::lock_guard<std::mutex> lock(local_mutex_);
        local_string_queue_[topic] = message;
    }
    try {
        auto& e = ensure_topic_channel(topics_, topic);
        (void)e;
        // 发布原始字节流；这与 wxz::core::FastddsChannel 的治理约定保持一致。
        e.channel->publish(reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
    } catch (...) {
        if (fallback_enabled_) {
            log_fallback("FastDDSCommunicator::send");
        } else {
            throw;
        }
    }
}

std::string FastDDSCommunicator::Impl::receive(const std::string& topic) {
    try {
        auto& e = ensure_topic_channel(topics_, topic);
        std::vector<std::uint8_t> msg;
        {
            std::lock_guard<std::mutex> lock(e.mu);
            if (!e.queue.empty()) {
                msg = std::move(e.queue.front());
                e.queue.pop_front();
            }
        }
        if (!msg.empty()) {
            return std::string(reinterpret_cast<const char*>(msg.data()), msg.size());
        }
    } catch (...) {
        if (!fallback_enabled_) throw;
    }
    if (!fallback_enabled_) return std::string();
    std::lock_guard<std::mutex> lock(local_mutex_);
    auto it_local = local_string_queue_.find(topic);
    if (it_local == local_string_queue_.end()) return std::string();
    std::string out = it_local->second;
    local_string_queue_.erase(it_local);
    log_fallback("FastDDSCommunicator::receive");
    return out;
}

void FastDDSCommunicator::Impl::sendDTO(const std::string& topic, const EventDTO& dto) {
    if (fallback_enabled_) {
        std::lock_guard<std::mutex> lock(local_mutex_);
        local_dto_queue_[topic] = dto;
    }
    try {
        auto& e = ensure_topic_channel(dto_topics_, topic);
        auto bytes = serialize_event_dto_to_cdr_bytes(dto);
        e.channel->publish(bytes.data(), bytes.size());
    } catch (...) {
        if (fallback_enabled_) {
            log_fallback("FastDDSCommunicator::sendDTO");
        } else {
            throw;
        }
    }
}

bool FastDDSCommunicator::Impl::receiveDTO(const std::string& topic, EventDTO& out) {
    try {
        auto& e = ensure_topic_channel(dto_topics_, topic);
        std::vector<std::uint8_t> msg;
        {
            std::lock_guard<std::mutex> lock(e.mu);
            if (!e.queue.empty()) {
                msg = std::move(e.queue.front());
                e.queue.pop_front();
            }
        }
        if (!msg.empty()) {
            return deserialize_event_dto_from_cdr_bytes(msg.data(), msg.size(), out);
        }
    } catch (...) {
        if (!fallback_enabled_) throw;
    }
    if (!fallback_enabled_) return false;
    std::lock_guard<std::mutex> lock(local_mutex_);
    auto it_local = local_dto_queue_.find(topic);
    if (it_local == local_dto_queue_.end()) return false;
    out = it_local->second;
    local_dto_queue_.erase(it_local);
    log_fallback("FastDDSCommunicator::receiveDTO");
    return true;
}

void FastDDSCommunicator::Impl::setPeers(const std::vector<std::string>& peers) {
    peers_ = peers; // currently stored for future routing; no transport change yet
}

void FastDDSCommunicator::Impl::setTopicQos(const std::string& topic, const TopicQosProfile& qos) {
    topic_qos_[topic] = qos;
}

FastDDSCommunicator::Impl::ChannelTopicEntry& FastDDSCommunicator::Impl::ensure_topic_channel(
    std::map<std::string, ChannelTopicEntry>& table,
    const std::string& topic) {
    auto& entry = table[topic];
    if (entry.channel) return entry;

    wxz::core::ChannelQoS qos;
    auto it_q = topic_qos_.find(topic);
    if (it_q != topic_qos_.end()) {
        qos = channel_qos_from_legacy(it_q->second);
    }
    entry.capacity = (qos.history == 0) ? 32 : std::max<std::size_t>(1, qos.history);

    // 立即订阅，让 receive() 只需做简单 pop()。
    entry.channel = std::make_unique<wxz::core::FastddsChannel>(domain_id_, topic, qos, max_payload_);
    entry.sub = entry.channel->subscribe_scoped([
        &entry
    ](const std::uint8_t* data, std::size_t size) {
        if (!data || size == 0) return;
        std::vector<std::uint8_t> copy;
        copy.resize(size);
        std::memcpy(copy.data(), data, size);
        std::lock_guard<std::mutex> lock(entry.mu);
        entry.queue.push_back(std::move(copy));
        while (entry.queue.size() > entry.capacity) {
            entry.queue.pop_front();
        }
    });
    return entry;
}
