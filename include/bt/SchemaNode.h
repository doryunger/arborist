#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace bt {

enum class SchemaNodeType : std::uint8_t {
    ACTION,
    CONDITION,
    SEQUENCE,
    SELECTOR,
    PARALLEL,
};

enum class SchemaPolicy : std::uint8_t {
    ALL,
    ANY,
    THRESHOLD,
};

struct SchemaNode {
    SchemaNodeType type{SchemaNodeType::ACTION};
    std::string name;
    std::string intent;
    SchemaPolicy policy{SchemaPolicy::ALL};
    std::size_t threshold{1};
    std::vector<std::unique_ptr<SchemaNode>> children;

    SchemaNode() = default;
    ~SchemaNode() = default;
    SchemaNode(const SchemaNode&) = delete;
    SchemaNode& operator=(const SchemaNode&) = delete;
    SchemaNode(SchemaNode&&) = default;
    SchemaNode& operator=(SchemaNode&&) = default;

    [[nodiscard]] std::unique_ptr<SchemaNode> deepClone() const;
};

struct StateDeclaration {
    std::string key;
    std::string type;
};

struct BehaviorSchema {
    std::string name;
    std::string condition;
    std::string intent;
    bool interruptible{true};
    std::unique_ptr<SchemaNode> tree;

    BehaviorSchema() = default;
    ~BehaviorSchema() = default;
    BehaviorSchema(const BehaviorSchema&) = delete;
    BehaviorSchema& operator=(const BehaviorSchema&) = delete;
    BehaviorSchema(BehaviorSchema&&) = default;
    BehaviorSchema& operator=(BehaviorSchema&&) = default;
};

struct SchemaDoc {
    std::string schemaVersion;
    std::string subtreeName;
    std::vector<std::string> imports;
    std::vector<StateDeclaration> stateDeclarations;
    std::vector<BehaviorSchema> behaviors;
};

}  // namespace bt
