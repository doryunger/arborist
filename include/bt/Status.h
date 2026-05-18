#pragma once

#include <cstdint>
#include <string_view>

namespace bt {

enum class Status : std::uint8_t {
    SUCCESS = 0,
    FAILURE = 1,
    RUNNING = 2,
};

inline std::string_view toString(Status status) noexcept {
    switch (status) {
        case Status::SUCCESS:
            return "SUCCESS";
        case Status::FAILURE:
            return "FAILURE";
        case Status::RUNNING:
            return "RUNNING";
    }
    return "UNKNOWN";
}

}  // namespace bt
