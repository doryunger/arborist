#pragma once

#include <cstddef>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "bt/DecisionEmitter.h"
#include "bt/Status.h"

namespace bt {

struct QueryFilter {
    std::optional<std::string> behaviorName;
    std::optional<Status> status;
    std::size_t fromTick{0};
    std::size_t toTick{std::numeric_limits<std::size_t>::max()};
};

// Read-only query interface over a DecisionEmitter's tick history.
class MonitorQuery {
public:
    explicit MonitorQuery(const DecisionEmitter& emitter) noexcept : emitter_(&emitter) {}

    [[nodiscard]] const std::deque<TickRecord>& all() const noexcept {
        return emitter_->history();
    }

    [[nodiscard]] std::vector<TickRecord> filter(const QueryFilter& queryFilter) const;

private:
    const DecisionEmitter* emitter_;
};

}  // namespace bt
