#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bt/BehaviorEntry.h"

namespace bt {

// Structural validator for a set of registered behaviors.
// Run before the first tick — warnings here are actionable at startup.
class Validator {
public:
    struct Warning {
        enum class Level : std::uint8_t { WARNING, ERROR };
        Level level;
        std::string behaviorName;
        std::string message;

        [[nodiscard]] bool isError() const noexcept { return level == Level::ERROR; }
    };

    [[nodiscard]] static std::vector<Warning> validate(
        const std::vector<BehaviorEntry>& entries);
};

}  // namespace bt
