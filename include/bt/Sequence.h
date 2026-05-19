#pragma once

#include <string>
#include <string_view>

#include "bt/CompositeNode.h"
#include "bt/Status.h"

namespace bt {

// Ticks children left-to-right. Returns FAILURE on the first child that fails
// (short-circuits). Returns RUNNING and remembers the current child index when
// a child returns RUNNING — on the next tick it resumes from that child, not
// from the start. Returns SUCCESS only when all children have succeeded.
class Sequence : public CompositeNode {
public:
    explicit Sequence(std::string name) : CompositeNode(std::move(name)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Sequence"; }

protected:
    [[nodiscard]] Status doTick() override;
};

}  // namespace bt
