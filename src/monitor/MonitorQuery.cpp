#include "bt/MonitorQuery.h"

namespace bt {

std::vector<TickRecord> MonitorQuery::filter(const QueryFilter& queryFilter) const {
    std::vector<TickRecord> results;
    for (const auto& record : emitter_->history()) {
        if (record.tickNumber < queryFilter.fromTick) { continue; }
        if (record.tickNumber > queryFilter.toTick) { continue; }
        if (queryFilter.behaviorName && record.behaviorName != *queryFilter.behaviorName) { continue; }
        if (queryFilter.status && record.result != *queryFilter.status) { continue; }
        results.push_back(record);
    }
    return results;
}

}  // namespace bt
