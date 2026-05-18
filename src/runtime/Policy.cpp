#include "bt/Policy.h"

namespace bt {

bool Policy::satisfied(std::size_t successes, std::size_t failures,
                        std::size_t total) const noexcept {
    switch (type_) {
        case Type::ALL:
            return successes == total;
        case Type::ANY:
            return successes >= 1;
        case Type::THRESHOLD:
            return successes >= threshold_;
    }
    return false;
}

bool Policy::failed(std::size_t successes, std::size_t failures,
                     std::size_t total) const noexcept {
    switch (type_) {
        case Type::ALL:
            return failures >= 1;
        case Type::ANY:
            return failures == total;
        case Type::THRESHOLD:
            return (total - failures) < threshold_;
    }
    return false;
}

std::string_view Policy::typeName() const noexcept {
    switch (type_) {
        case Type::ALL:
            return "ALL";
        case Type::ANY:
            return "ANY";
        case Type::THRESHOLD:
            return "THRESHOLD";
    }
    return "UNKNOWN";
}

}  // namespace bt
