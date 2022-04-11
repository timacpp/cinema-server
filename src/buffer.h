#ifndef CINEMA_SERVER_BUFFER_H
#define CINEMA_SERVER_BUFFER_H

#include <type_traits>

namespace {
    /** Gives the number of octets required to store a variable. */
    template<typename T>
    inline constexpr size_t bytes(const T& data) {
        if constexpr (std::is_convertible_v<T, std::string>) {
            return data.size();
        } else {
            return sizeof(data);
        }
    }
}

/**
 * Writes a generic data to a buffer of a given size.
 * @tparam Arg type of a variable
 * @param dest buffer
 * @param src data to write
 * @param size octets per source
 * @return @p size
 */
template<typename Arg>
inline size_t buffer_write(char* dest, Arg&& src, size_t size) {
    if constexpr (std::is_convertible_v<Arg, std::string>) {
        memcpy(dest, const_cast<char*>(src.c_str()), size);
    } else {
        memcpy(dest, reinterpret_cast<char*>(&src), size);
    }

    return size;
}

/**
 * Writes generic data to a buffer and returns it's size in octets.
 * See buffer_write(char*, Arg&&, size_t)
 */
template<typename Arg>
inline size_t buffer_write(char* dest, Arg&& src) {
    return buffer_write(dest, src, bytes(src));
}

/**
 * Writes an arbitrary number of generic data to a buffer
 * and returns number of written octets. See buffer_write(char*, Arg&&)
 */
template<typename Arg, typename... Args>
inline size_t buffer_write(char* dest, Arg&& src_head, Args&&... src_tail) {
    size_t written = buffer_write(dest, std::forward<Arg>(src_head));
    return written + buffer_write(dest + written, std::forward<Args>(src_tail)...);
}


/**
 * Reads a pointer from buffer
 * @tparam Arg underlying type of a pointer
 * @param buffer buffer
 * @param offset number of bytes to skip
 * @return pointer to @p buffer + @p offset
 */
template<typename Arg>
inline Arg* buffer_read(char* buffer, size_t offset) {
    return reinterpret_cast<Arg*>(buffer + offset);
}

/**
 * Converts buffer to a string.
 * @param buffer buffer
 * @param offset number of bytes to skip
 * @param len lengths a string to extract
 * @return string
 */
inline std::string buffer_to_string(char* buffer, size_t offset, size_t len = 1) {
    return {buffer + offset, len};
}

#endif //CINEMA_SERVER_BUFFER_H
