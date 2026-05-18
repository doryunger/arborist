#pragma once

#include <functional>
#include <string>
#include <vector>

#include "bt/BehaviorEntry.h"
#include "bt/Status.h"

namespace bt {

// Fluent API for registering behaviors. Each call to behavior() starts a new
// entry. Subsequent calls modify the most recently started entry.
//
// Example:
//   BehaviorBuilder builder;
//   builder
//     .behavior("attack")
//       .when([] { return enemyVisible; })
//       .onTick([] { return Status::RUNNING; })
//       .interruptible(true)
//     .behavior("patrol")
//       .onTick([] { return Status::SUCCESS; });
class BehaviorBuilder {
public:
    BehaviorBuilder& behavior(std::string name);
    BehaviorBuilder& when(std::function<bool()> condition);
    BehaviorBuilder& onEnter(std::function<void()> callback);
    BehaviorBuilder& onTick(std::function<Status()> callback);
    BehaviorBuilder& onExit(std::function<void()> callback);
    BehaviorBuilder& interruptible(bool value);

    [[nodiscard]] const std::vector<BehaviorEntry>& entries() const noexcept {
        return entries_;
    }

private:
    std::vector<BehaviorEntry> entries_;
};

}  // namespace bt
