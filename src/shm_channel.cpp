#include "shm_channel.h"

#include "observability.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace wxz::core {

namespace {
constexpr std::uint32_t kMagic = 0x53484d43; // SHMC
inline std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }
} // namespace

std::string ShmChannel::normalize_name(const std::string& n) {
    if (n.empty()) throw std::invalid_argument("shm name empty");
    if (n[0] == '/') return n;
    return "/" + n;
}

std::string ShmChannel::sem_name_from(const std::string& n) {
    std::string base = normalize_name(n);
    return base + "_sem";
}

ShmChannel::ShmChannel(std::string name, std::size_t capacity, std::size_t slot_size, bool create)
    : name_(normalize_name(name)), sem_name_(sem_name_from(name)), region_bytes_(0), owner_(create) {
    if (slot_size < sizeof(std::uint32_t)) {
        throw std::invalid_argument("slot_size too small");
    }
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        throw std::invalid_argument("capacity must be power of two and > 0");
    }
    region_bytes_ = align_up(sizeof(Header) + capacity * slot_size, 64);

    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    shm_fd_ = ::shm_open(name_.c_str(), flags, 0666);
    if (shm_fd_ < 0) {
        throw std::runtime_error("shm_open failed");
    }
    if (create) {
        if (ftruncate(shm_fd_, static_cast<off_t>(region_bytes_)) != 0) {
            ::close(shm_fd_);
            throw std::runtime_error("ftruncate failed");
        }
    }

    void* addr = mmap(nullptr, region_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (addr == MAP_FAILED) {
        ::close(shm_fd_);
        throw std::runtime_error("mmap failed");
    }
    base_ = static_cast<std::uint8_t*>(addr);
    hdr_ = reinterpret_cast<Header*>(base_);

    sem_t* s = nullptr;
    if (create) {
        ::sem_unlink(sem_name_.c_str());
        s = ::sem_open(sem_name_.c_str(), O_CREAT | O_EXCL, 0666, 0);
    } else {
        s = ::sem_open(sem_name_.c_str(), 0);
    }
    if (s == SEM_FAILED) {
        munmap(base_, region_bytes_);
        ::close(shm_fd_);
        throw std::runtime_error("sem_open failed");
    }
    sem_ = s;

    if (create) {
        hdr_->head.store(0, std::memory_order_relaxed);
        hdr_->tail.store(0, std::memory_order_relaxed);
        hdr_->capacity = static_cast<std::uint32_t>(capacity);
        hdr_->slot_size = static_cast<std::uint32_t>(slot_size);
        hdr_->magic = kMagic;
    } else {
        if (hdr_->magic != kMagic) {
            munmap(base_, region_bytes_);
            ::close(shm_fd_);
            ::sem_close(sem_);
            throw std::runtime_error("shm magic mismatch");
        }
    }
}

ShmChannel::~ShmChannel() {
    stop();
    if (base_) munmap(base_, region_bytes_);
    if (shm_fd_ >= 0) ::close(shm_fd_);
    if (sem_) ::sem_close(sem_);
    if (owner_) {
        ::shm_unlink(name_.c_str());
        ::sem_unlink(sem_name_.c_str());
    }
}

ShmChannel::SlotView ShmChannel::slot(std::uint32_t idx) const {
    std::size_t stride = hdr_->slot_size;
    std::uint8_t* ptr = base_ + sizeof(Header) + static_cast<std::size_t>(idx) * stride;
    return {ptr, stride};
}

bool ShmChannel::publish(const std::uint8_t* data, std::size_t size) {
    if (!hdr_) return false;
    std::uint32_t head = hdr_->head.load(std::memory_order_acquire);
    std::uint32_t tail = hdr_->tail.load(std::memory_order_acquire);
    std::uint32_t cap = hdr_->capacity;
    if (static_cast<std::uint32_t>(head - tail) >= cap) {
        publish_fail_.fetch_add(1, std::memory_order_relaxed);
        if (wxz::core::has_metrics_sink()) {
            wxz::core::metrics().counter_add("wxz.shm.publish.fail", 1, {});
        }
        return false; // full
    }
    std::uint32_t idx = head & (cap - 1);
    auto sv = slot(idx);
    std::uint32_t max_payload = hdr_->slot_size - static_cast<std::uint32_t>(sizeof(std::uint32_t));
    std::uint32_t copy = static_cast<std::uint32_t>(std::min<std::size_t>(size, max_payload));
    std::memcpy(sv.ptr + sizeof(std::uint32_t), data, copy);
    std::memcpy(sv.ptr, &copy, sizeof(std::uint32_t));
    hdr_->head.store(head + 1, std::memory_order_release);
    ::sem_post(sem_);
    publish_success_.fetch_add(1, std::memory_order_relaxed);
    if (wxz::core::has_metrics_sink()) {
        wxz::core::metrics().counter_add("wxz.shm.publish.success", 1, {});
        wxz::core::metrics().histogram_observe("wxz.shm.publish.bytes", static_cast<double>(copy), {});
    }
    return true;
}

void ShmChannel::subscribe(Handler handler) {
    auto sub = subscribe_scoped(std::move(handler), nullptr);
    sub.detach();
}

Subscription ShmChannel::subscribe_scoped(Handler handler, void* owner) {
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        id = next_handler_id_++;
        handlers_.push_back(HandlerEntry{.id = id, .owner = owner, .handler = std::move(handler)});
    }

    bool expected = false;
    if (running_.compare_exchange_strong(expected, true)) {
        worker_ = std::thread([this]() { dispatch_loop(); });
    }

    return Subscription([this, id]() {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(), [&](const HandlerEntry& e) {
                           return e.id == id;
                       }),
                       handlers_.end());
    });
}

void ShmChannel::unsubscribe_owner(void* owner) {
    if (!owner) return;
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(), [&](const HandlerEntry& e) {
                       return e.owner == owner;
                   }),
                   handlers_.end());
}

void ShmChannel::stop() {
    const bool was_running = running_.exchange(false);
    if (was_running) {
        if (sem_) {
            ::sem_post(sem_); // wake
        }
        if (worker_.joinable()) worker_.join();
    }
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handlers_.clear();
    }
}

void ShmChannel::dispatch_loop() {
    while (running_.load(std::memory_order_relaxed)) {
        // wait for a message
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50 * 1000 * 1000; // 50ms
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
        int r = ::sem_timedwait(sem_, &ts);
        if (r != 0) {
            continue;
        }
        std::uint32_t tail = hdr_->tail.load(std::memory_order_acquire);
        std::uint32_t head = hdr_->head.load(std::memory_order_acquire);
        std::uint32_t cap = hdr_->capacity;
        if (tail == head) {
            continue; // nothing
        }
        std::uint32_t idx = tail & (cap - 1);
        auto sv = slot(idx);
        std::uint32_t sz = 0;
        std::memcpy(&sz, sv.ptr, sizeof(std::uint32_t));
        const std::uint8_t* payload = sv.ptr + sizeof(std::uint32_t);
        std::vector<Handler> copy_handlers;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            copy_handlers.reserve(handlers_.size());
            for (const auto& e : handlers_) copy_handlers.push_back(e.handler);
        }
        for (auto& h : copy_handlers) {
            if (h) h(payload, sz);
        }
        messages_delivered_.fetch_add(1, std::memory_order_relaxed);
        hdr_->tail.store(tail + 1, std::memory_order_release);
    }
}

} // namespace wxz::core
