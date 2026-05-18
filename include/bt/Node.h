#pragma once

#include <string>
#include <string_view>

#include "bt/Status.h"

namespace bt {

class Node {
public:
    explicit Node(std::string name) : name_(std::move(name)) {}

    virtual ~Node() = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    [[nodiscard]] virtual Status tick() = 0;

    virtual void reset() {}

    [[nodiscard]] std::string_view name() const noexcept { return name_; }

private:
    std::string name_;
};

}  // namespace bt
