#include "bt/Node.h"

namespace bt {

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local std::uint64_t gCurrentTickId{0};
}  // namespace

void Node::setCurrentTickId(std::uint64_t tickId) noexcept {
    gCurrentTickId = tickId;
}

Status Node::tick() {
    lastTickId_ = gCurrentTickId;
    return doTick();
}

}  // namespace bt
