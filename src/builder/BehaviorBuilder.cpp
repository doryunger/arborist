#include "bt/BehaviorBuilder.h"

#include <cassert>

namespace bt {

BehaviorBuilder& BehaviorBuilder::behavior(std::string name) {
    entries_.push_back(BehaviorEntry{.name = std::move(name)});
    return *this;
}

BehaviorBuilder& BehaviorBuilder::when(std::function<bool()> condition) {
    assert(!entries_.empty());
    entries_.back().condition = std::move(condition);
    return *this;
}

BehaviorBuilder& BehaviorBuilder::onEnter(std::function<void()> callback) {
    assert(!entries_.empty());
    entries_.back().onEnter = std::move(callback);
    return *this;
}

BehaviorBuilder& BehaviorBuilder::onTick(std::function<Status()> callback) {
    assert(!entries_.empty());
    entries_.back().onTick = std::move(callback);
    return *this;
}

BehaviorBuilder& BehaviorBuilder::onExit(std::function<void()> callback) {
    assert(!entries_.empty());
    entries_.back().onExit = std::move(callback);
    return *this;
}

BehaviorBuilder& BehaviorBuilder::interruptible(bool value) {
    assert(!entries_.empty());
    entries_.back().interruptible = value;
    return *this;
}

}  // namespace bt
