#pragma once

#include <string>
#include <string_view>

#include "bt/CompositeNode.h"
#include "bt/Status.h"

namespace bt {

// Ticks children left-to-right. Returns SUCCESS on the first child that
// succeeds (short-circuits). Returns RUNNING and remembers the current child
// index when a child returns RUNNING — on the next tick it resumes from that
// child. Returns FAILURE only when all children have failed.
class Selector : public CompositeNode {
public:
    explicit Selector(std::string name) : CompositeNode(std::move(name)) {}

    [[nodiscard]] Status tick() override;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Selector"; }
};

}  // namespace bt
