#pragma once

#include <functional>
#include <string>

#include "bt/Status.h"

namespace bt {

// A single registered behavior definition produced by BehaviorBuilder.
struct BehaviorEntry {
    std::string name;
    std::function<bool()> condition;     // null means always valid (default behavior)
    std::function<void()> onEnter;       // null means no-op
    std::function<Status()> onTick;      // required — null is caught by Validator
    std::function<void()> onExit;        // null means no-op
    bool interruptible{true};
};

}  // namespace bt
