#include "bt/TreeAssembler.h"

#include <utility>

#include "bt/BehaviorAction.h"
#include "bt/Condition.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"

namespace bt {

BehaviorTree TreeAssembler::assemble(std::vector<BehaviorEntry> entries, Blackboard blackboard) {
    auto root = std::make_unique<Selector>("root");

    std::vector<BehaviorMeta> metas;
    metas.reserve(entries.size());

    for (auto& entry : entries) {
        auto seq = std::make_unique<Sequence>(entry.name);

        if (entry.condition) {
            seq->addChild(
                std::make_unique<Condition>(entry.name + "_condition", entry.condition));
        }

        seq->addChild(std::make_unique<BehaviorAction>(entry.name + "_action",
                                                        std::move(entry.onEnter),
                                                        std::move(entry.onTick),
                                                        std::move(entry.onExit)));

        root->addChild(std::move(seq));
        metas.push_back(BehaviorMeta{.condition = entry.condition,
                                      .interruptible = entry.interruptible});
    }

    return BehaviorTree(std::move(root), std::move(blackboard), std::move(metas));
}

}  // namespace bt
