#ifndef CINEMA_SERVER_BUFFER_H
#define CINEMA_SERVER_BUFFER_H

#include <type_traits>

namespace {
    template<typename T>
    inline constexpr size_t bytes(const T& data) {
        if constexpr (std::is_convertible_v<T, std::string>) {
            return data.size();
        } else {
            return sizeof(data);
        }
    }
}

template<typename Arg>
inline size_t buffer_write(char* dest, Arg&& src, size_t size) {
    if constexpr (std::is_convertible_v<Arg, std::string>) {
        memcpy(dest, const_cast<char*>(src.c_str()), size);
    } else {
        memcpy(dest, reinterpret_cast<char*>(&src), size);
    }

    return size;
}

template<typename Arg>
inline size_t buffer_write(char* dest, Arg&& src) {
    return buffer_write(dest, src, bytes(src));
}

template<typename Arg, typename... Args>
inline size_t buffer_write(char* dest, Arg&& src_head, Args&&... src_tail) {
    size_t written = buffer_write(dest, std::forward<Arg>(src_head));
    return written + buffer_write(dest + written, std::forward<Args>(src_tail)...);
}

template<typename Arg>
inline Arg* buffer_read(char* buffer, size_t offset) {
    return reinterpret_cast<Arg*>(buffer + offset);
}

#endif //CINEMA_SERVER_BUFFER_H
