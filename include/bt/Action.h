#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "bt/Status.h"
#include "bt/Node.h"

namespace bt {

// Leaf node that wraps a callback returning Status.
// Called on every tick. For enter/exit lifecycle use BehaviorAction
// (produced by TreeAssembler from builder definitions).
class Action : public Node {
public:
    using Callback = std::function<Status()>;

    Action(std::string name, Callback callback)
        : Node(std::move(name)), callback_(std::move(callback)) {}

    [[nodiscard]] Status tick() override { return callback_(); }
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Action"; }

private:
    Callback callback_;
};

}  // namespace bt
