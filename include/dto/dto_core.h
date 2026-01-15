#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wxz::dto {

struct TypeInfo {
    std::string name;          // 唯一类型标识，例如 "sensor.image2d"
    uint32_t    version{1};    // 版本号，追加字段时递增
    std::string content_type;  // 序列化格式，如 "cdr"/"json"/"cbor"
    uint64_t    schema_hash{0};
    std::string compat_policy; // 可选："ignore_unknown" 等策略描述
};

class Serializer {
public:
    virtual ~Serializer() = default;
    virtual bool write_uint32(uint32_t v) = 0;
    virtual bool write_uint64(uint64_t v) = 0;
    virtual bool write_int32(int32_t v) = 0;
    virtual bool write_int64(int64_t v) = 0;
    virtual bool write_bool(bool v) = 0;
    virtual bool write_uint8(uint8_t v) = 0;
    virtual bool write_float(float v) = 0;
    virtual bool write_double(double v) = 0;
    virtual bool write_string(const std::string &v) = 0;
    virtual bool write_bytes(const std::vector<uint8_t> &v) = 0;
    virtual const std::vector<uint8_t> &buffer() const = 0;
};

class Deserializer {
public:
    virtual ~Deserializer() = default;
    virtual bool read_uint32(uint32_t &v) = 0;
    virtual bool read_uint64(uint64_t &v) = 0;
    virtual bool read_int32(int32_t &v) = 0;
    virtual bool read_int64(int64_t &v) = 0;
    virtual bool read_bool(bool &v) = 0;
    virtual bool read_uint8(uint8_t &v) = 0;
    virtual bool read_float(float &v) = 0;
    virtual bool read_double(double &v) = 0;
    virtual bool read_string(std::string &v) = 0;
    virtual bool read_bytes(std::vector<uint8_t> &v) = 0;
    virtual bool eof() const = 0;
};

class IDto {
public:
    virtual ~IDto() = default;
    virtual const TypeInfo &type() const = 0;
    virtual uint32_t version() const = 0;
    virtual bool serialize(Serializer &out) const = 0;
    virtual bool deserialize(Deserializer &in) = 0;
    virtual std::unique_ptr<IDto> clone() const = 0;
};

using FactoryFn = std::function<std::unique_ptr<IDto>()>;

class TypeRegistry {
public:
    static TypeRegistry &instance();

    bool registerFactory(const TypeInfo &info, FactoryFn fn);
    std::unique_ptr<IDto> create(const std::string &name) const;
    std::unique_ptr<IDto> create(const TypeInfo &info) const;
    std::vector<TypeInfo> list() const;

private:
    TypeRegistry() = default;
    TypeRegistry(const TypeRegistry &) = delete;
    TypeRegistry &operator=(const TypeRegistry &) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::pair<TypeInfo, FactoryFn>> factories_;
};

// 简易二进制序列化器（示例；可替换为 FastDDS CDR 后端）
class BinarySerializer : public Serializer {
public:
    explicit BinarySerializer(std::vector<uint8_t> &buf) : buf_(buf) { buf_.clear(); }
    bool write_uint32(uint32_t v) override;
    bool write_uint64(uint64_t v) override;
    bool write_int32(int32_t v) override;
    bool write_int64(int64_t v) override;
    bool write_bool(bool v) override;
    bool write_uint8(uint8_t v) override;
    bool write_float(float v) override;
    bool write_double(double v) override;
    bool write_string(const std::string &v) override;
    bool write_bytes(const std::vector<uint8_t> &v) override;
    const std::vector<uint8_t> &buffer() const override { return buf_; }
private:
    std::vector<uint8_t> &buf_;
};

class BinaryDeserializer : public Deserializer {
public:
    explicit BinaryDeserializer(const std::vector<uint8_t> &buf) : buf_(buf) {}
    bool read_uint32(uint32_t &v) override;
    bool read_uint64(uint64_t &v) override;
    bool read_int32(int32_t &v) override;
    bool read_int64(int64_t &v) override;
    bool read_bool(bool &v) override;
    bool read_uint8(uint8_t &v) override;
    bool read_float(float &v) override;
    bool read_double(double &v) override;
    bool read_string(std::string &v) override;
    bool read_bytes(std::vector<uint8_t> &v) override;
    bool eof() const override { return offset_ >= buf_.size(); }
private:
    const std::vector<uint8_t> &buf_;
    size_t offset_{0};
};

// FastDDS/fastcdr 适配版（定长缓冲，简单演示；若超出容量则返回 false）
class CdrSerializer : public Serializer {
public:
    explicit CdrSerializer(std::vector<uint8_t> &buf, size_t initial_reserve = 64 * 1024);
    bool write_uint32(uint32_t v) override;
    bool write_uint64(uint64_t v) override;
    bool write_int32(int32_t v) override;
    bool write_int64(int64_t v) override;
    bool write_bool(bool v) override;
    bool write_uint8(uint8_t v) override;
    bool write_float(float v) override;
    bool write_double(double v) override;
    bool write_string(const std::string &v) override;
    bool write_bytes(const std::vector<uint8_t> &v) override;
    const std::vector<uint8_t> &buffer() const override { return buf_; }
private:
    bool ensure_capacity(size_t need_bytes);
    std::vector<uint8_t> &buf_;
    size_t capacity_;
    size_t used_{0};
};

class CdrDeserializer : public Deserializer {
public:
    explicit CdrDeserializer(const std::vector<uint8_t> &buf);
    CdrDeserializer(const uint8_t* data, size_t size);
    bool read_uint32(uint32_t &v) override;
    bool read_uint64(uint64_t &v) override;
    bool read_int32(int32_t &v) override;
    bool read_int64(int64_t &v) override;
    bool read_bool(bool &v) override;
    bool read_uint8(uint8_t &v) override;
    bool read_float(float &v) override;
    bool read_double(double &v) override;
    bool read_string(std::string &v) override;
    bool read_bytes(std::vector<uint8_t> &v) override;
    bool eof() const override { return offset_ >= size_; }
private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t offset_{0};
};

} // namespace wxz::dto
