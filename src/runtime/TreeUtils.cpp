#include "bt/TreeUtils.h"

#include <cstddef>
#include <string>

namespace bt {

namespace {

std::string nodeLabel(const Node& node) {
    std::string label = "[";
    label += node.typeName();
    label += "]";
    if (!node.name().empty()) {
        label += " ";
        label += node.name();
    }
    return label;
}

void buildTree(const Node& node, const std::string& prefix, bool isLast, std::string& output) {
    output += prefix;
    output += isLast ? "└── " : "├── ";
    output += nodeLabel(node);
    output += "\n";

    auto kids = node.children();
    std::string childPrefix = prefix + (isLast ? "    " : "│   ");
    for (std::size_t idx = 0; idx < kids.size(); ++idx) {
        buildTree(*kids[idx], childPrefix, idx == kids.size() - 1, output);
    }
}

}  // namespace

std::string treeToString(const Node& root) {
    std::string output = nodeLabel(root) + "\n";
    auto kids = root.children();
    for (std::size_t idx = 0; idx < kids.size(); ++idx) {
        buildTree(*kids[idx], "", idx == kids.size() - 1, output);
    }
    return output;
}

std::size_t countNodes(const Node& root) {
    std::size_t count = 1;
    for (const auto& child : root.children()) {
        count += countNodes(*child);
    }
    return count;
}

std::size_t countSchemaNodes(const SchemaNode& root) {
    std::size_t count = 1;
    for (const auto& child : root.children) {
        count += countSchemaNodes(*child);
    }
    return count;
}

}  // namespace bt
