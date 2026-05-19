#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "bt/Node.h"
#include "bt/Status.h"

namespace bt {

// Transparent scope boundary node. Wraps a subtree root and ticks it
// unchanged — semantics are identical to the unwrapped subtree.
// Scope boundaries show in the tree viewer and can be tested in isolation.
class SubtreeScope : public Node {
public:
    explicit SubtreeScope(std::string name, std::unique_ptr<Node> subtreeRoot);

    ~SubtreeScope() override = default;
    SubtreeScope(const SubtreeScope&) = delete;
    SubtreeScope& operator=(const SubtreeScope&) = delete;
    SubtreeScope(SubtreeScope&&) = delete;
    SubtreeScope& operator=(SubtreeScope&&) = delete;

    [[nodiscard]] std::string_view typeName() const noexcept override { return "SubtreeScope"; }
    [[nodiscard]] std::span<const std::unique_ptr<Node>> children() const noexcept override;
    void reset() override;

protected:
    [[nodiscard]] Status doTick() override;

private:
    std::vector<std::unique_ptr<Node>> children_;
};

}  // namespace bt
