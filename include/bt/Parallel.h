#pragma once

#include <string>
#include <string_view>

#include "bt/CompositeNode.h"
#include "bt/Policy.h"
#include "bt/Status.h"

namespace bt {

// Ticks ALL children every tick and aggregates results via a Policy.
// Unlike Sequence/Selector, Parallel does not use RUNNING resumption —
// all children are ticked on every call regardless of their previous result.
class Parallel : public CompositeNode {
public:
    explicit Parallel(std::string name, Policy policy = Policy::all())
        : CompositeNode(std::move(name)), policy_(policy) {}

    [[nodiscard]] Status tick() override;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Parallel"; }
    [[nodiscard]] const Policy& policy() const noexcept { return policy_; }

private:
    Policy policy_;
};

}  // namespace bt
