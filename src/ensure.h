#ifndef CINEMA_SERVER_ENSURE_H
#define CINEMA_SERVER_ENSURE_H

#include <iostream>

template<typename Arg, typename... Args>
inline void ensure(bool condition, const Arg& head, const Args&... args) {
    if (!condition) {
        ((std::cerr << "Error: " << head) <<
            ... << (std::cerr << " ", args)) << std::endl;
        exit(EXIT_FAILURE);
    }
}

template<typename Arg, typename... Args>
inline void quit(const Arg& head, const Args&... args) {
    ensure(false, head, args...);
}

#endif //CINEMA_SERVER_ENSURE_H
