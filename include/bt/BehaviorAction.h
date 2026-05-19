#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "bt/Node.h"
#include "bt/Status.h"

namespace bt {

// Action node produced by TreeAssembler. Manages the enter/tick/exit
// lifecycle for a registered behavior.
class BehaviorAction : public Node {
public:
    BehaviorAction(std::string name, std::function<void()> onEnter,
                   std::function<Status()> onTick, std::function<void()> onExit)
        : Node(std::move(name)),
          onEnter_(std::move(onEnter)),
          onTick_(std::move(onTick)),
          onExit_(std::move(onExit)) {}

    void reset() override;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "BehaviorAction"; }

protected:
    [[nodiscard]] Status doTick() override;

private:
    std::function<void()> onEnter_;
    std::function<Status()> onTick_;
    std::function<void()> onExit_;
    bool entered_{false};
};

}  // namespace bt
