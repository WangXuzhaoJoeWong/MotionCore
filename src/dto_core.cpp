#include "dto/dto_core.h"

#include <algorithm>
#include <cstring>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastcdr/exceptions/NotEnoughMemoryException.h>
#include "internal/fastcdr_compat.h"

namespace wxz::dto {

TypeRegistry &TypeRegistry::instance() {
    static TypeRegistry inst;
    return inst;
}

bool TypeRegistry::registerFactory(const TypeInfo &info, FactoryFn fn) {
    if (!fn || info.name.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(info.name);
    if (it != factories_.end()) {
        // 已存在同名类型则拒绝注册，保持类型唯一性
        return false;
    }
    factories_.emplace(info.name, std::make_pair(info, std::move(fn)));
    return true;
}

std::unique_ptr<IDto> TypeRegistry::create(const std::string &name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(name);
    if (it == factories_.end()) {
        return nullptr;
    }
    return it->second.second();
}

std::unique_ptr<IDto> TypeRegistry::create(const TypeInfo &info) const {
    return create(info.name);
}

std::vector<TypeInfo> TypeRegistry::list() const {
    std::vector<TypeInfo> out;
    std::lock_guard<std::mutex> lock(mutex_);
    out.reserve(factories_.size());
    for (const auto &kv : factories_) {
        out.push_back(kv.second.first);
    }
    return out;
}

// 二进制写入（小端）
static void write_primitive(std::vector<uint8_t> &buf, const void *data, size_t len) {
    const auto *ptr = static_cast<const uint8_t *>(data);
    buf.insert(buf.end(), ptr, ptr + len);
}

bool BinarySerializer::write_uint32(uint32_t v) {
    write_primitive(buf_, &v, sizeof(v));
    return true;
}

bool BinarySerializer::write_uint64(uint64_t v) {
    write_primitive(buf_, &v, sizeof(v));
    return true;
}

bool BinarySerializer::write_int32(int32_t v) {
    write_primitive(buf_, &v, sizeof(v));
    return true;
}

bool BinarySerializer::write_int64(int64_t v) {
    write_primitive(buf_, &v, sizeof(v));
    return true;
}

bool BinarySerializer::write_bool(bool v) {
    uint8_t b = v ? 1 : 0;
    write_primitive(buf_, &b, sizeof(b));
    return true;
}

bool BinarySerializer::write_uint8(uint8_t v) {
    write_primitive(buf_, &v, sizeof(v));
    return true;
}

bool BinarySerializer::write_float(float v) {
    write_primitive(buf_, &v, sizeof(v));
    return true;
}

bool BinarySerializer::write_double(double v) {
    write_primitive(buf_, &v, sizeof(v));
    return true;
}

bool BinarySerializer::write_string(const std::string &v) {
    uint32_t len = static_cast<uint32_t>(v.size());
    write_primitive(buf_, &len, sizeof(len));
    if (!v.empty()) {
        write_primitive(buf_, v.data(), v.size());
    }
    return true;
}

bool BinarySerializer::write_bytes(const std::vector<uint8_t> &v) {
    uint32_t len = static_cast<uint32_t>(v.size());
    write_primitive(buf_, &len, sizeof(len));
    if (!v.empty()) {
        write_primitive(buf_, v.data(), v.size());
    }
    return true;
}

static bool read_primitive(const std::vector<uint8_t> &buf, size_t &offset, void *out, size_t len) {
    if (offset + len > buf.size()) return false;
    std::memcpy(out, buf.data() + offset, len);
    offset += len;
    return true;
}

bool BinaryDeserializer::read_uint32(uint32_t &v) {
    return read_primitive(buf_, offset_, &v, sizeof(v));
}

bool BinaryDeserializer::read_uint64(uint64_t &v) {
    return read_primitive(buf_, offset_, &v, sizeof(v));
}

bool BinaryDeserializer::read_int32(int32_t &v) {
    return read_primitive(buf_, offset_, &v, sizeof(v));
}

bool BinaryDeserializer::read_int64(int64_t &v) {
    return read_primitive(buf_, offset_, &v, sizeof(v));
}

bool BinaryDeserializer::read_bool(bool &v) {
    uint8_t b = 0;
    if (!read_primitive(buf_, offset_, &b, sizeof(b))) return false;
    v = (b != 0);
    return true;
}

bool BinaryDeserializer::read_uint8(uint8_t &v) {
    return read_primitive(buf_, offset_, &v, sizeof(v));
}

bool BinaryDeserializer::read_float(float &v) {
    return read_primitive(buf_, offset_, &v, sizeof(v));
}

bool BinaryDeserializer::read_double(double &v) {
    return read_primitive(buf_, offset_, &v, sizeof(v));
}

bool BinaryDeserializer::read_string(std::string &v) {
    uint32_t len = 0;
    if (!read_uint32(len)) return false;
    if (offset_ + len > buf_.size()) return false;
    v.assign(reinterpret_cast<const char *>(buf_.data() + offset_), len);
    offset_ += len;
    return true;
}

bool BinaryDeserializer::read_bytes(std::vector<uint8_t> &v) {
    uint32_t len = 0;
    if (!read_uint32(len)) return false;
    if (offset_ + len > buf_.size()) return false;
    v.assign(buf_.begin() + offset_, buf_.begin() + offset_ + len);
    offset_ += len;
    return true;
}

// --- Fast CDR 适配 ---
CdrSerializer::CdrSerializer(std::vector<uint8_t> &buf, size_t initial_reserve)
    : buf_(buf) {
    buf_.clear();
    buf_.reserve(initial_reserve);
    capacity_ = buf_.capacity();
    used_ = 0;
}

bool CdrSerializer::ensure_capacity(size_t need_bytes) {
    if (need_bytes <= buf_.capacity()) {
        capacity_ = buf_.capacity();
        if (buf_.size() < capacity_) {
            buf_.resize(capacity_);
        }
        return true;
    }
    size_t grow = std::max(need_bytes, buf_.capacity() * 2 + 1024);
    buf_.reserve(grow);
    capacity_ = buf_.capacity();
    buf_.resize(capacity_);
    return true;
}

bool CdrSerializer::write_uint32(uint32_t v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(v) + 4; // extra padding for alignment
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_uint64(uint64_t v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(v) + 8; // pad for alignment
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_int32(int32_t v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(v) + 4;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_int64(int64_t v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(v) + 8;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_bool(bool v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(uint8_t) + 4;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 256);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_uint8(uint8_t v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(v) + 4;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 256);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_float(float v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(v) + 4;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_double(double v) {
    for (int retry = 0; retry < 4; ++retry) {
        size_t need = used_ + sizeof(v) + 8;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_string(const std::string &v) {
    for (int retry = 0; retry < 5; ++retry) {
        // fastcdr writes length + data + null terminator; add slack for alignment
        size_t need = used_ + sizeof(uint32_t) + v.size() + 8;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            wxz::internal::fastcdr_compat::write(cdr, v);
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + v.size() + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool CdrSerializer::write_bytes(const std::vector<uint8_t> &v) {
    for (int retry = 0; retry < 5; ++retry) {
        size_t need = used_ + sizeof(uint32_t) + v.size() + 8;
        ensure_capacity(need);

        eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(buf_.data()), capacity_);
        eprosima::fastcdr::Cdr cdr(fb);
        cdr.jump(used_);
        try {
            uint32_t len = static_cast<uint32_t>(v.size());
            wxz::internal::fastcdr_compat::write(cdr, len);
            if (len > 0) {
                wxz::internal::fastcdr_compat::serialize_array(cdr, v.data(), len);
            }
            used_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
            buf_.resize(used_);
            return true;
        } catch (const eprosima::fastcdr::exception::NotEnoughMemoryException &) {
            ensure_capacity(capacity_ * 2 + v.size() + 1024);
            continue;
        } catch (...) {
            return false;
        }
    }
    return false;
}

CdrDeserializer::CdrDeserializer(const std::vector<uint8_t> &buf) : buf_(buf) {}

bool CdrDeserializer::read_uint32(uint32_t &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_uint64(uint64_t &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_int32(int32_t &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_int64(int64_t &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_bool(bool &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_uint8(uint8_t &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_float(float &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_double(double &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_string(std::string &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        wxz::internal::fastcdr_compat::read(cdr, v);
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

bool CdrDeserializer::read_bytes(std::vector<uint8_t> &v) {
    eprosima::fastcdr::FastBuffer fb(reinterpret_cast<char *>(const_cast<uint8_t *>(buf_.data())), buf_.size());
    eprosima::fastcdr::Cdr cdr(fb);
    cdr.jump(offset_);
    try {
        uint32_t len = 0;
        wxz::internal::fastcdr_compat::read(cdr, len);
        v.resize(len);
        if (len > 0) {
            wxz::internal::fastcdr_compat::deserialize_array(cdr, v.data(), len);
        }
        offset_ = wxz::internal::fastcdr_compat::serialized_length(cdr);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace wxz::dto
