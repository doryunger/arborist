#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "bt/Node.h"
#include "bt/Status.h"

namespace bt {

// Leaf node that wraps a boolean predicate.
// Returns SUCCESS if the predicate returns true, FAILURE otherwise.
// Never returns RUNNING.
class Condition : public Node {
public:
    using Predicate = std::function<bool()>;

    Condition(std::string name, Predicate predicate)
        : Node(std::move(name)), predicate_(std::move(predicate)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Condition"; }

protected:
    [[nodiscard]] Status doTick() override {
        return predicate_() ? Status::SUCCESS : Status::FAILURE;
    }

private:
    Predicate predicate_;
};

}  // namespace bt
