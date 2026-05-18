#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bt {

// Aggregation policy for a Parallel node.
// ALL  — all children must succeed.
// ANY  — one child succeeding is enough.
// THRESHOLD(n) — at least n children must succeed.
class Policy {
public:
    [[nodiscard]] static Policy all() noexcept { return {Type::ALL, 0}; }
    [[nodiscard]] static Policy any() noexcept { return {Type::ANY, 0}; }
    [[nodiscard]] static Policy threshold(std::size_t minSuccess) noexcept {
        return {Type::THRESHOLD, minSuccess};
    }

    [[nodiscard]] bool satisfied(std::size_t successes, std::size_t failures,
                                  std::size_t total) const noexcept;
    [[nodiscard]] bool failed(std::size_t successes, std::size_t failures,
                               std::size_t total) const noexcept;

    [[nodiscard]] std::string_view typeName() const noexcept;
    [[nodiscard]] std::size_t threshold() const noexcept { return threshold_; }

private:
    enum class Type : std::uint8_t { ALL, ANY, THRESHOLD };

    Type type_{Type::ALL};
    std::size_t threshold_{0};

    constexpr Policy(Type type, std::size_t threshold) noexcept
        : type_(type), threshold_(threshold) {}
};

}  // namespace bt
