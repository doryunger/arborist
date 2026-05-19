#pragma once

#include <string>

#include "bt/Node.h"

namespace bt {

// Serializes a node tree to JSON.
// Each node becomes: {"name":"...","type":"...","children":[...]}
class TreeSerializer {
public:
    [[nodiscard]] static std::string toJson(const Node& root);
};

}  // namespace bt
