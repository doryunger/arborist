#pragma once

#include <cstdint>
#include <memory>
#include <span>
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

    // Non-virtual: stamps lastTickId_ then delegates to doTick().
    // BehaviorTree calls setCurrentTickId() before ticking the root so every
    // node that participates in a tick gets stamped with the same ID.
    [[nodiscard]] Status tick();

    [[nodiscard]] virtual std::string_view typeName() const noexcept = 0;
    [[nodiscard]] virtual std::span<const std::unique_ptr<Node>> children() const noexcept {
        return {};
    }

    virtual void reset() {}

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] std::uint64_t lastTickId()     const noexcept { return lastTickId_; }
    [[nodiscard]] Status         lastStatus()    const noexcept { return lastStatus_; }

    // Called by BehaviorTree before each root tick so stamps are unique per tick.
    static void setCurrentTickId(std::uint64_t tickId) noexcept;

protected:
    [[nodiscard]] virtual Status doTick() = 0;

private:
    std::string   name_;
    std::uint64_t lastTickId_{0};
    Status        lastStatus_{Status::RUNNING};
};

}  // namespace bt
