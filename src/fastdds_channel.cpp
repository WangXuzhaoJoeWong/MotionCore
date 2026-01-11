#include "fastdds_channel.h"

#include "internal/dds_security_precheck.h"
#include "internal/fastcdr_compat.h"
#include "internal/fastdds_participant_factory.h"
#include "observability.h"

#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/topic/qos/TopicQos.hpp>
#include <fastdds/rtps/common/SerializedPayload.h>

#ifdef MEMBER_ID_INVALID
#undef MEMBER_ID_INVALID
#endif

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace wxz::core {

namespace {

struct RawMsg {
    std::vector<std::uint8_t> data;
};

class RawMsgType : public eprosima::fastdds::dds::TopicDataType {
public:
    explicit RawMsgType(std::size_t max_payload) : max_payload_(max_payload) {
        setName("WxzRawBytes");
        // Allow extra headroom for possible CDR alignment/padding.
        // Encapsulation(4) + len(uint32=4) + bytes + alignment headroom.
        m_typeSize = static_cast<uint32_t>(max_payload_ + 24);
        m_isGetKeyDefined = false;
    }

    bool serialize(void* data, eprosima::fastrtps::rtps::SerializedPayload_t* payload) override {
        RawMsg* msg = static_cast<RawMsg*>(data);
        if (msg->data.size() > max_payload_) return false;
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->max_size);
        eprosima::fastcdr::Cdr ser(fastbuffer);

        // FastDDS expects the CDR encapsulation to be present.
        payload->encapsulation = wxz::internal::fastcdr_compat::is_big_endian(ser) ? CDR_BE : CDR_LE;
        wxz::internal::fastcdr_compat::serialize_encapsulation(ser);

        wxz::internal::fastcdr_compat::write(ser, static_cast<uint32_t>(msg->data.size()));
        if (!msg->data.empty()) {
            wxz::internal::fastcdr_compat::serialize_array(ser, msg->data.data(), msg->data.size());
        }
        payload->length = wxz::internal::fastcdr_compat::serialized_length_u32(ser);
        return true;
    }

    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload, void* data) override {
        RawMsg* msg = static_cast<RawMsg*>(data);
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->length);
        eprosima::fastcdr::Cdr deser(fastbuffer);

        wxz::internal::fastcdr_compat::read_encapsulation(deser);
        payload->encapsulation = wxz::internal::fastcdr_compat::is_big_endian(deser) ? CDR_BE : CDR_LE;

        uint32_t len = 0;
        wxz::internal::fastcdr_compat::read(deser, len);
        if (len > max_payload_) return false;
        msg->data.resize(len);
        if (len > 0) {
            wxz::internal::fastcdr_compat::deserialize_array(deser, msg->data.data(), len);
        }
        return true;
    }

    std::function<uint32_t()> getSerializedSizeProvider(void* data) override {
        RawMsg* msg = static_cast<RawMsg*>(data);
        return [msg]() { return static_cast<uint32_t>(4 + 4 + msg->data.size()); };
    }

    void* createData() override { return new RawMsg(); }
    void deleteData(void* data) override { delete static_cast<RawMsg*>(data); }
    bool getKey(void*, eprosima::fastrtps::rtps::InstanceHandle_t*, bool) override { return false; }

private:
    std::size_t max_payload_;
};

class ReaderListener final : public eprosima::fastdds::dds::DataReaderListener {
public:
    using Entry = FastddsChannel::HandlerEntry;

    explicit ReaderListener(std::vector<Entry>& entries,
                            std::mutex& m,
                            std::atomic<std::uint64_t>& recv_counter,
                                                        const std::string& topic_name,
                            std::atomic<bool>& stopping,
                            std::atomic<std::uint32_t>& inflight)
                : entries_(entries),
                    mutex_(m),
                    recv_counter_(recv_counter),
                    topic_name_(topic_name),
                    stopping_(stopping),
                    inflight_(inflight) {}

    void on_data_available(eprosima::fastdds::dds::DataReader* reader) override {
        if (stopping_.load(std::memory_order_relaxed)) return;

        struct InflightGuard {
            std::atomic<std::uint32_t>& v;
            explicit InflightGuard(std::atomic<std::uint32_t>& vv) : v(vv) { v.fetch_add(1, std::memory_order_relaxed); }
            ~InflightGuard() { v.fetch_sub(1, std::memory_order_relaxed); }
        } guard(inflight_);

        RawMsg msg;
        eprosima::fastdds::dds::SampleInfo info;
        while (reader->take_next_sample(&msg, &info) == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            if (info.instance_state != eprosima::fastdds::dds::ALIVE_INSTANCE_STATE) continue;
            std::vector<FastddsChannel::Handler> copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                copy.reserve(entries_.size());
                for (const auto& e : entries_) copy.push_back(e.handler);
            }
            for (auto& h : copy) {
                if (h) h(msg.data.data(), msg.data.size());
            }
            recv_counter_.fetch_add(1, std::memory_order_relaxed);

            if (wxz::core::has_metrics_sink()) {
                wxz::core::metrics().counter_add("wxz.fastdds.recv.messages", 1, {{"topic", topic_name_}});
                wxz::core::metrics().histogram_observe(
                    "wxz.fastdds.recv.bytes", static_cast<double>(msg.data.size()), {{"topic", topic_name_}});
            }
            if (wxz::core::has_trace_hook()) {
                wxz::core::trace().event("wxz.fastdds.recv", {{"topic", topic_name_}});
            }
        }
    }

private:
    std::vector<Entry>& entries_;
    std::mutex& mutex_;
    std::atomic<std::uint64_t>& recv_counter_;
    const std::string& topic_name_;
    std::atomic<bool>& stopping_;
    std::atomic<std::uint32_t>& inflight_;
};

} // namespace

FastddsChannel::FastddsChannel(int domain_id, std::string topic, const ChannelQoS& qos, std::size_t max_payload)
    : FastddsChannel(domain_id, std::move(topic), qos, max_payload, /*enable_pub=*/true, /*enable_sub=*/true) {}

FastddsChannel::FastddsChannel(int domain_id,
                               std::string topic,
                               const ChannelQoS& qos,
                               std::size_t max_payload,
                               bool enable_pub,
                               bool enable_sub)
    : domain_id_(domain_id), topic_name_(std::move(topic)), max_payload_(max_payload) {
    using namespace eprosima::fastdds::dds;

    // If DDS-Security is enabled via FASTDDS_ENVIRONMENT_FILE, fail-fast when critical
    // security artifacts are missing. This avoids "process lives but cannot communicate"
    // and makes misconfiguration diagnosable in CI and production.
    wxz::core::internal::precheck_dds_security_from_fastdds_env_file(std::getenv("FASTDDS_ENVIRONMENT_FILE"));

    // Optional: allow users to control FastDDS behavior via XML profiles.
    // - If WXZ_FASTDDS_PROFILES_FILE is set, it must be readable and loadable.
    // - If WXZ_FASTDDS_PARTICIPANT_PROFILE is set, it must exist.
    // See internal factory for details.
    participant_ = wxz::core::internal::create_fastdds_participant_from_env(domain_id_);
    if (!participant_) {
        throw std::runtime_error("FastDDS participant create failed");
    }

    // TypeSupport owns the TopicDataType pointer.
    type_ = TypeSupport(new RawMsgType(max_payload_));
    type_.register_type(participant_);

    // IMPORTANT: Always create both Publisher and Subscriber entities.
    // Empirically, some FastDDS configurations will not match endpoints correctly
    // across processes if a participant only creates one side.
    // We still avoid self-subscription by conditionally creating only the writer/reader.
    publisher_ = participant_->create_publisher(PublisherQos(), nullptr);
    if (!publisher_) {
        cleanup();
        throw std::runtime_error("FastDDS publisher create failed");
    }

    subscriber_ = participant_->create_subscriber(SubscriberQos(), nullptr);
    if (!subscriber_) {
        cleanup();
        throw std::runtime_error("FastDDS subscriber create failed");
    }

    TopicQos tqos;
    topic_ = participant_->create_topic(topic_name_.c_str(), type_->getName(), tqos);
    if (!topic_) {
        cleanup();
        throw std::runtime_error("FastDDS topic create failed");
    }

    DataWriterQos wqos;
    DataReaderQos rqos;
    apply_qos(qos, wqos, rqos, qos.history == 0 ? 32 : qos.history); // default depth 32 when keep_all
        if (qos.realtime_hint) {
            wqos.publish_mode().kind = SYNCHRONOUS_PUBLISH_MODE;
        }

    if (enable_pub) {
        writer_ = publisher_->create_datawriter(topic_, wqos, nullptr);
        if (!writer_) {
            cleanup();
            throw std::runtime_error("FastDDS writer create failed");
        }
    }

    if (enable_sub) {
        listener_ = std::make_unique<ReaderListener>(handlers_, handler_mutex_, messages_received_, topic_name_, stopping_, callbacks_inflight_);
        reader_ = subscriber_->create_datareader(topic_, rqos, listener_.get());
        if (!reader_) {
            cleanup();
            throw std::runtime_error("FastDDS reader create failed");
        }
    }
}

FastddsChannel::~FastddsChannel() { cleanup(); }

void FastddsChannel::cleanup() {
    using namespace eprosima::fastdds::dds;
    if (!participant_) {
        type_.reset();
        return;
    }

    stopping_.store(true, std::memory_order_relaxed);

    // Best-effort safety: prevent callbacks into user handlers while tearing down.
    if (reader_) {
        try {
            reader_->set_listener(nullptr);
        } catch (...) {
            // ignore
        }
    }

    // Wait briefly for any in-flight listener callbacks to finish before deleting DDS entities.
    // This mitigates shutdown-time races observed as intermittent SIGSEGV in short-lived processes.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (callbacks_inflight_.load(std::memory_order_relaxed) != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Prefer explicit teardown order over delete_contained_entities().
    // In practice, some FastDDS versions/configs can be sensitive to implicit teardown
    // when DataReader callbacks are in-flight.
    if (subscriber_ && reader_) {
        try {
            (void)subscriber_->delete_datareader(reader_);
        } catch (...) {
            // ignore
        }
        reader_ = nullptr;
    }
    listener_.reset();

    if (publisher_ && writer_) {
        try {
            (void)publisher_->delete_datawriter(writer_);
        } catch (...) {
            // ignore
        }
        writer_ = nullptr;
    }

    if (participant_ && topic_) {
        try {
            (void)participant_->delete_topic(topic_);
        } catch (...) {
            // ignore
        }
        topic_ = nullptr;
    }

    if (participant_ && subscriber_) {
        try {
            (void)participant_->delete_subscriber(subscriber_);
        } catch (...) {
            // ignore
        }
        subscriber_ = nullptr;
    }

    if (participant_ && publisher_) {
        try {
            (void)participant_->delete_publisher(publisher_);
        } catch (...) {
            // ignore
        }
        publisher_ = nullptr;
    }

    eprosima::fastdds::dds::DomainParticipantFactory::get_instance()->delete_participant(participant_);

    participant_ = nullptr;
    type_.reset();
}

bool FastddsChannel::publish(const std::uint8_t* data, std::size_t size) {
    if (!writer_) return false;
    if (size > max_payload_) return false;
    auto t0 = std::chrono::steady_clock::now();
    RawMsg msg;
    msg.data.resize(size);
    if (size > 0) {
        std::memcpy(msg.data.data(), data, size);
    }
    auto rc = writer_->write(&msg);

    // FastDDS API differences: depending on the installed version/headers,
    // DataWriter::write may return bool or a ReturnCode-like enum.
    bool ok = false;
    if constexpr (std::is_same_v<decltype(rc), bool>) {
        ok = rc;
    } else {
        ok = (static_cast<int>(rc) == 0);
    }
    // Optional tolerance: allow demos to proceed even if writer reports error.
    // Set WXZ_DDS_IGNORE_WRITE_ERRORS=1 to treat RETCODE_ERROR as success.
    const char* ignore_err = std::getenv("WXZ_DDS_IGNORE_WRITE_ERRORS");
    const bool tolerate = (ignore_err && (*ignore_err == '1' || std::strcmp(ignore_err, "true") == 0));
    auto dt = std::chrono::steady_clock::now() - t0;
    const auto duration_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    last_publish_duration_ns_.store(duration_ns);
    if (ok || tolerate) {
        publish_success_.fetch_add(1);
    } else {
        publish_fail_.fetch_add(1);
        using eprosima::fastdds::dds::PublicationMatchedStatus;
        PublicationMatchedStatus st{};
        try {
            (void)writer_->get_publication_matched_status(st);
            std::cerr << "[fastdds] publish failed retcode=" << static_cast<int>(rc) << " matched_subscribers=" << st.current_count << " total=" << st.total_count << "\n";
        } catch (...) {
            std::cerr << "[fastdds] publish failed retcode=" << static_cast<int>(rc) << " (status unavailable)\n";
        }
    }

    if (wxz::core::has_metrics_sink()) {
        wxz::core::metrics().counter_add(ok || tolerate ? "wxz.fastdds.publish.success" : "wxz.fastdds.publish.fail",
                                         1,
                                         {{"topic", topic_name_}});
        wxz::core::metrics().histogram_observe("wxz.fastdds.publish.duration_ns",
                                               static_cast<double>(duration_ns),
                                               {{"topic", topic_name_}});
        wxz::core::metrics().histogram_observe("wxz.fastdds.publish.bytes",
                                               static_cast<double>(size),
                                               {{"topic", topic_name_}});
    }
    if (wxz::core::has_trace_hook()) {
        wxz::core::trace().event("wxz.fastdds.publish",
                                 {{"topic", topic_name_}, {"ok", (ok || tolerate) ? "1" : "0"}});
    }
    return ok || tolerate;
}

void FastddsChannel::subscribe(Handler handler) {
    auto sub = subscribe_scoped(std::move(handler), nullptr);
    sub.detach();
}

Subscription FastddsChannel::subscribe_scoped(Handler handler, void* owner) {
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        id = next_handler_id_++;
        handlers_.push_back(HandlerEntry{.id = id, .owner = owner, .handler = std::move(handler)});
    }

    return Subscription([this, id]() {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(), [&](const HandlerEntry& e) {
                           return e.id == id;
                       }),
                       handlers_.end());
    });
}

void FastddsChannel::unsubscribe_owner(void* owner) {
    if (!owner) return;
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(), [&](const HandlerEntry& e) {
                       return e.owner == owner;
                   }),
                   handlers_.end());
}

void FastddsChannel::stop() {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handlers_.clear();
}

void FastddsChannel::apply_qos(const ChannelQoS& qos,
                               eprosima::fastdds::dds::DataWriterQos& wqos,
                               eprosima::fastdds::dds::DataReaderQos& rqos,
                               std::size_t history_depth) {
    using namespace eprosima::fastdds::dds;
    auto to_duration = [](std::uint64_t ns) {
        return eprosima::fastrtps::Duration_t(static_cast<int32_t>(ns / 1000000000),
                                              static_cast<uint32_t>(ns % 1000000000));
    };
    ReliabilityQosPolicyKind rel = qos.reliability == ChannelQoS::Reliability::reliable
                                       ? RELIABLE_RELIABILITY_QOS
                                       : BEST_EFFORT_RELIABILITY_QOS;
    wqos.reliability().kind = rel;
    rqos.reliability().kind = rel;

    if (qos.history == 0) {
        wqos.history().kind = KEEP_ALL_HISTORY_QOS;
        rqos.history().kind = KEEP_ALL_HISTORY_QOS;
        // keep_all implies resource limits must be large; use depth hint
        wqos.resource_limits().max_samples = static_cast<int32_t>(history_depth);
        rqos.resource_limits().max_samples = static_cast<int32_t>(history_depth);
    } else {
        wqos.history().kind = KEEP_LAST_HISTORY_QOS;
        rqos.history().kind = KEEP_LAST_HISTORY_QOS;
        wqos.history().depth = static_cast<int32_t>(history_depth);
        rqos.history().depth = static_cast<int32_t>(history_depth);
        wqos.resource_limits().max_samples = static_cast<int32_t>(history_depth);
        rqos.resource_limits().max_samples = static_cast<int32_t>(history_depth);
    }

    if (qos.latency_budget_ns > 0) {
        wqos.latency_budget().duration = to_duration(qos.latency_budget_ns);
        rqos.latency_budget().duration = to_duration(qos.latency_budget_ns);
    }
    if (qos.deadline_ns > 0) {
        wqos.deadline().period = to_duration(qos.deadline_ns);
        rqos.deadline().period = to_duration(qos.deadline_ns);
    }

    if (qos.lifespan_ns > 0) {
        wqos.lifespan().duration = to_duration(qos.lifespan_ns);
        rqos.lifespan().duration = to_duration(qos.lifespan_ns);
    }
    if (qos.time_based_filter_ns > 0) {
        rqos.time_based_filter().minimum_separation = to_duration(qos.time_based_filter_ns);
    }

    DurabilityQosPolicyKind dur = qos.durability == ChannelQoS::Durability::transient_local
                                      ? TRANSIENT_LOCAL_DURABILITY_QOS
                                      : VOLATILE_DURABILITY_QOS;
    wqos.durability().kind = dur;
    rqos.durability().kind = dur;

    LivelinessQosPolicyKind liv = qos.liveliness == ChannelQoS::Liveliness::manual_by_topic
                                      ? MANUAL_BY_TOPIC_LIVELINESS_QOS
                                      : AUTOMATIC_LIVELINESS_QOS;
    wqos.liveliness().kind = liv;
    rqos.liveliness().kind = liv;

    OwnershipQosPolicyKind own = qos.ownership == ChannelQoS::Ownership::exclusive
                                     ? EXCLUSIVE_OWNERSHIP_QOS
                                     : SHARED_OWNERSHIP_QOS;
    wqos.ownership().kind = own;
    rqos.ownership().kind = own;
    if (own == EXCLUSIVE_OWNERSHIP_QOS) {
        wqos.ownership_strength().value = qos.ownership_strength;
    }

    wqos.transport_priority().value = qos.transport_priority;

    wqos.publish_mode().kind = qos.async_publish ? ASYNCHRONOUS_PUBLISH_MODE : SYNCHRONOUS_PUBLISH_MODE;
    if (qos.realtime_hint) {
        // Force sync publish in realtime mode to avoid background thread scheduling jitter.
        wqos.publish_mode().kind = SYNCHRONOUS_PUBLISH_MODE;
    }
}

} // namespace wxz::core
