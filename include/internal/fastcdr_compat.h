#pragma once

#ifdef MEMBER_ID_INVALID
#undef MEMBER_ID_INVALID
#endif

#include <fastcdr/Cdr.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace wxz::internal::fastcdr_compat {

template <typename CdrT>
auto serialized_length_impl(CdrT& cdr, int) -> decltype(cdr.getSerializedDataLength(), std::size_t{}) {
    return static_cast<std::size_t>(cdr.getSerializedDataLength());
}

template <typename CdrT>
auto serialized_length_impl(CdrT& cdr, long) -> decltype(cdr.get_serialized_data_length(), std::size_t{}) {
    return static_cast<std::size_t>(cdr.get_serialized_data_length());
}

inline std::size_t serialized_length(eprosima::fastcdr::Cdr& cdr) { return serialized_length_impl(cdr, 0); }

inline uint32_t serialized_length_u32(eprosima::fastcdr::Cdr& cdr) {
    return static_cast<uint32_t>(serialized_length(cdr));
}

inline bool is_big_endian(eprosima::fastcdr::Cdr& cdr) {
    return cdr.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS;
}

inline void serialize_encapsulation(eprosima::fastcdr::Cdr& cdr) { cdr.serialize_encapsulation(); }

inline void read_encapsulation(eprosima::fastcdr::Cdr& cdr) { cdr.read_encapsulation(); }

template <typename T>
inline void write(eprosima::fastcdr::Cdr& cdr, const T& v) {
    cdr << v;
}

template <typename T>
inline void read(eprosima::fastcdr::Cdr& cdr, T& v) {
    cdr >> v;
}

template <typename CdrT, typename T>
auto serialize_array_impl(CdrT& cdr, const T* data, std::size_t size, int) -> decltype(cdr.serializeArray(data, size), void()) {
    cdr.serializeArray(data, size);
}

template <typename CdrT, typename T>
auto serialize_array_impl(CdrT& cdr, const T* data, std::size_t size, long) -> decltype(cdr.serialize_array(data, size), void()) {
    cdr.serialize_array(data, size);
}

template <typename T>
inline void serialize_array(eprosima::fastcdr::Cdr& cdr, const T* data, std::size_t size) {
    serialize_array_impl(cdr, data, size, 0);
}

template <typename CdrT, typename T>
auto deserialize_array_impl(CdrT& cdr, T* data, std::size_t size, int) -> decltype(cdr.deserializeArray(data, size), void()) {
    cdr.deserializeArray(data, size);
}

template <typename CdrT, typename T>
auto deserialize_array_impl(CdrT& cdr, T* data, std::size_t size, long)
    -> decltype(cdr.deserialize_array(data, size), void()) {
    cdr.deserialize_array(data, size);
}

template <typename T>
inline void deserialize_array(eprosima::fastcdr::Cdr& cdr, T* data, std::size_t size) {
    deserialize_array_impl(cdr, data, size, 0);
}

} // namespace wxz::internal::fastcdr_compat
