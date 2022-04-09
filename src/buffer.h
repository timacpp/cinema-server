#ifndef CINEMA_SERVER_BUFFER_H
#define CINEMA_SERVER_BUFFER_H

#include <type_traits>

namespace {
    template<typename T>
    using enable_if_string_t = std::enable_if_t<std::is_same_v<T, std::string>>;

    template<typename T>
    using enable_if_not_string_t = std::enable_if_t<!std::is_same_v<T, std::string>>;

    template<typename T>
    inline constexpr size_t bytes(const T& data, enable_if_not_string_t<T>* = nullptr) {
        return sizeof(data);
    }

    template<typename T>
    inline constexpr size_t bytes(const T& data, enable_if_string_t<T>* = nullptr) {
        return data.size();
    }
}

template<typename Arg>
inline size_t buffer_write(char* dest, Arg&& src, enable_if_not_string_t<Arg>* = nullptr) {
    memcpy(dest, reinterpret_cast<char*>(&src), sizeof(src));
    return sizeof(src);
}

template<typename Arg>
inline size_t buffer_write(char* dest, Arg&& src, enable_if_string_t<Arg>* = nullptr) {
    memcpy(dest, const_cast<char*>(src.c_str()), src.size());
    return src.size();
}

template<typename Arg, typename... Args>
inline size_t buffer_write(char* dest, Arg&& src_head, Args&&... src_tail) {
    return buffer_write(dest, std::forward<Arg>(src_head)) +
           buffer_write(dest + bytes(src_head), std::forward<Args>(src_tail)...);
}

template<typename Arg>
inline Arg* buffer_read(char* buffer, size_t offset) {
    return reinterpret_cast<Arg*>(buffer + offset);
}

#endif //CINEMA_SERVER_BUFFER_H
