#pragma once

#include <string>

#include "bt/Node.h"

namespace bt {

// Returns an ASCII representation of the tree rooted at node.
// Example output:
//   [Selector] root
//   ├── [Sequence] attack_sequence
//   │   ├── [TestLeaf] enemy_visible
//   │   └── [TestLeaf] attack
//   └── [TestLeaf] patrol
[[nodiscard]] std::string treeToString(const Node& root);

// Count total nodes in the tree rooted at root (including root itself).
[[nodiscard]] std::size_t countNodes(const Node& root);

}  // namespace bt
