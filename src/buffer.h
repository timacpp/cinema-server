#ifndef CINEMA_SERVER_BUFFER_H
#define CINEMA_SERVER_BUFFER_H

namespace {
    template<typename T>
    inline constexpr size_t bytes(const T& data) {
        if constexpr (std::is_same_v<T, char*>) {
            return strlen(data);
        } else {
            return sizeof(data);
        }
    }
}

template<typename Arg> // TODO: why it works then src is reference???????
inline size_t buffer_write_n(char* dest, Arg& src, size_t size) {
    if constexpr (std::is_same_v<Arg, char*>) {
        memcpy(dest, src, size);
    } else {
        memcpy(dest, reinterpret_cast<char*>(&src), size);
    }

    return size;
}

template<typename Arg>
inline size_t buffer_write(char* dest, Arg&& src) {
    return buffer_write_n(dest, src, bytes(src));
}

template<typename Arg, typename... Args>
inline size_t buffer_write(char* dest, Arg&& src_head, Args&&... src_tail) {
    return buffer_write(dest, src_head) +
           buffer_write(dest + bytes(src_head), src_tail...);
}

template<typename T>
inline T* buffer_read(char* buffer, size_t offset = 0) {
    return reinterpret_cast<T*>(buffer + offset);
}

#endif //CINEMA_SERVER_BUFFER_H
