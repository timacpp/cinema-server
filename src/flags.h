#ifndef CINEMA_SERVER_FLAGS_H
#define CINEMA_SERVER_FLAGS_H

#include <regex>
#include <limits>
#include <sstream>
#include <optional>
#include <unordered_map>

#include "ensure.h"

namespace {
    /* Numerical base of input flag values */
    constexpr int IN_BASE = 10;

    /* Number of arguments per flag: name and value */
    constexpr size_t FLAG_DATA_LEN = 2;

    template<typename T>
    inline T convert_flag(const std::string& value) {
        if constexpr (std::is_same_v<T, std::string>) {
            return value;
        } else if constexpr (std::is_arithmetic_v<T>) {
            char* end;
            int64_t parsed = strtoll(value.c_str(), &end, IN_BASE);

            if (*end != 0) {
                throw std::invalid_argument("Flag illegal value");
            } else if (parsed > static_cast<int64_t>(std::numeric_limits<T>::max())) {
                throw std::invalid_argument("Flag value overflow");
            } else if (parsed < static_cast<int64_t>(std::numeric_limits<T>::min())) {
                throw std::invalid_argument("Flag value underflow");
            }

            return static_cast<T>(parsed);
        }

        quit("Unexpected flag type:", typeid(T).name());
    }

    inline std::regex build_flags_regex(const std::string& names) {
        std::string pattern;

        for (size_t i = 0; i < names.size(); i++) {
            pattern += "(-" + std::string(1, names[i]) + ")";

            if (i < names.size() - 1) {
                pattern += "|";
            }
        }

        return std::regex(pattern);
    }
}

/* Mapping of flag labels to their string values */
using flag_map = std::unordered_map<std::string, std::string>;

inline flag_map create_flag_map(int argc, char** argv, const std::string& names) {
    ensure(argc <= 1 + FLAG_DATA_LEN * names.size(), "Too many flags given.");

    flag_map flags;
    std::regex name_regex(build_flags_regex(names));

    /* Iterate over pairs: argv[i] - flag label, argv[i + 1] - flag value */
    for (size_t i = 1; i < argc; i += FLAG_DATA_LEN) {
        std::string name(argv[i]);

        ensure(std::regex_match(name, name_regex), "Unexpected flag", name);
        ensure(i + 1 < argc, "No value for flag", name);

        flags[name] = argv[i + 1];
    }

    return flags;
}

template<typename T>
inline std::optional<T> get_flag(const flag_map& flags, const std::string& name) {
    auto flag_it = flags.find(name);

    if (flag_it == flags.end())
        return std::nullopt;

    try {
        return convert_flag<T>(flag_it->second);
    } catch (const std::invalid_argument& e) {
        quit(e.what(), "for", name);
        return std::nullopt;
    }
}

template<typename T>
inline T get_flag_required(const flag_map& flags, const std::string& name) {
    try {
        return get_flag<T>(flags, name).value();
    } catch (const std::bad_optional_access& e) {
        quit("Flag", name, "is required");
        return std::is_arithmetic_v<T> ? static_cast<T>(0) : T();
    }
}

#endif //CINEMA_SERVER_FLAGS_H
