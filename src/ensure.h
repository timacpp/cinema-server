#ifndef CINEMA_SERVER_ENSURE_H
#define CINEMA_SERVER_ENSURE_H

#include <iostream>

template<typename Arg, typename... Args>
inline void display(const std::string& type, const Arg& head, const Args&... args) {
    ((std::cerr << type + ": " << head) << ... << (std::cerr << " ", args)) << std::endl;
}

template<typename Arg, typename... Args>
inline void alert(const Arg& head, const Args&... args) {
    display("Error", head, args...);
}

template<typename Arg, typename... Args>
inline void debug(const Arg& head, const Args&... args) {
#ifndef NDEBUG
    display("DEBUG", head, args...);
#endif
}

template<typename Arg, typename... Args>
inline void ensure(bool condition, const Arg& head, const Args&... args) {
    if (!condition) {
        alert(head, args...);
        exit(EXIT_FAILURE);
    }
}

template<typename Arg, typename... Args>
inline void quit(const Arg& head, const Args&... args) {
    ensure(false, head, args...);
}

#endif //CINEMA_SERVER_ENSURE_H
