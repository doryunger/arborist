#pragma once

#include <cassert>
#include <memory>
#include <span>
#include <vector>

#include "bt/Node.h"

namespace bt {

class CompositeNode : public Node {
public:
    explicit CompositeNode(std::string name) : Node(std::move(name)) {}

    void addChild(std::unique_ptr<Node> child) {
        assert(child != nullptr);
        children_.push_back(std::move(child));
    }

    // Convenience for tests and builders: adds a child and returns a raw pointer to it.
    template <typename T>
    T* addChildAndGet(std::unique_ptr<T> child) {
        assert(child != nullptr);
        T* ptr = child.get();
        children_.push_back(std::move(child));
        return ptr;
    }

    [[nodiscard]] std::span<const std::unique_ptr<Node>> children() const noexcept override {
        return children_;
    }

    void reset() override;

protected:
    [[nodiscard]] std::size_t currentChildIndex() const noexcept { return currentChildIndex_; }
    void advanceChildIndex() noexcept { ++currentChildIndex_; }

private:
    std::size_t currentChildIndex_{0};
    std::vector<std::unique_ptr<Node>> children_;
};

}  // namespace bt
