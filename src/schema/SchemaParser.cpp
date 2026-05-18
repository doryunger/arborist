#include "bt/SchemaParser.h"

#include <yaml-cpp/yaml.h>

#include <memory>
#include <string>

namespace bt {

namespace {

std::unique_ptr<SchemaNode> parseNode(const YAML::Node& node);

void parseChildren(const YAML::Node& node, SchemaNode& out) {
    if (!node["children"]) {
        return;
    }
    for (const auto& child : node["children"]) {
        out.children.push_back(parseNode(child));
    }
}

std::unique_ptr<SchemaNode> parseLeaf(SchemaNodeType type, const YAML::Node& node) {
    if (!node["name"]) {
        throw SchemaParseError("leaf node missing required field 'name'");
    }
    auto schemaNode = std::make_unique<SchemaNode>();
    schemaNode->type = type;
    schemaNode->name = node["name"].as<std::string>();
    return schemaNode;
}

std::unique_ptr<SchemaNode> parseParallel(const YAML::Node& node) {
    auto schemaNode = std::make_unique<SchemaNode>();
    schemaNode->type = SchemaNodeType::PARALLEL;

    if (node["policy"]) {
        auto policyStr = node["policy"].as<std::string>();
        if (policyStr == "all") {
            schemaNode->policy = SchemaPolicy::ALL;
        } else if (policyStr == "any") {
            schemaNode->policy = SchemaPolicy::ANY;
        } else if (policyStr == "threshold") {
            schemaNode->policy = SchemaPolicy::THRESHOLD;
            if (node["threshold"]) {
                schemaNode->threshold = node["threshold"].as<std::size_t>();
            }
        } else {
            throw SchemaParseError("unknown parallel policy: " + policyStr);
        }
    }

    parseChildren(node, *schemaNode);
    return schemaNode;
}

std::unique_ptr<SchemaNode> parseNode(const YAML::Node& node) {
    if (!node["type"]) {
        throw SchemaParseError("node missing required field 'type'");
    }

    auto typeStr = node["type"].as<std::string>();
    std::unique_ptr<SchemaNode> schemaNode;

    if (typeStr == "action") {
        schemaNode = parseLeaf(SchemaNodeType::ACTION, node);
    } else if (typeStr == "condition") {
        schemaNode = parseLeaf(SchemaNodeType::CONDITION, node);
    } else if (typeStr == "sequence") {
        schemaNode = std::make_unique<SchemaNode>();
        schemaNode->type = SchemaNodeType::SEQUENCE;
        parseChildren(node, *schemaNode);
    } else if (typeStr == "selector") {
        schemaNode = std::make_unique<SchemaNode>();
        schemaNode->type = SchemaNodeType::SELECTOR;
        parseChildren(node, *schemaNode);
    } else if (typeStr == "parallel") {
        schemaNode = parseParallel(node);
    } else {
        throw SchemaParseError("unknown node type: " + typeStr);
    }

    if (node["intent"]) {
        schemaNode->intent = node["intent"].as<std::string>();
    }

    return schemaNode;
}

BehaviorSchema parseBehavior(const YAML::Node& node) {
    if (!node["name"]) {
        throw SchemaParseError("behavior missing required field 'name'");
    }
    BehaviorSchema behavior;
    behavior.name = node["name"].as<std::string>();
    if (node["when"]) {
        behavior.condition = node["when"].as<std::string>();
    }
    if (node["intent"]) {
        behavior.intent = node["intent"].as<std::string>();
    }
    if (node["interruptible"]) {
        behavior.interruptible = node["interruptible"].as<bool>();
    }
    if (node["tree"]) {
        behavior.tree = parseNode(node["tree"]);
    }
    return behavior;
}

}  // namespace

SchemaDoc SchemaParser::parse(std::string_view yaml) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string(yaml));
    } catch (const YAML::Exception& ex) {
        throw SchemaParseError(ex.what());
    }

    if (!root["schema_version"]) {
        throw SchemaParseError("missing required field 'schema_version'");
    }

    SchemaDoc doc;
    doc.schemaVersion = root["schema_version"].as<std::string>();

    if (root["subtree"]) {
        doc.subtreeName = root["subtree"].as<std::string>();
    }

    if (root["import"]) {
        for (const auto& imp : root["import"]) {
            doc.imports.push_back(imp.as<std::string>());
        }
    }

    if (root["state"]) {
        for (const auto& entry : root["state"]) {
            StateDeclaration decl;
            decl.key = entry["key"].as<std::string>();
            decl.type = entry["type"].as<std::string>();
            doc.stateDeclarations.push_back(std::move(decl));
        }
    }

    if (root["behaviors"]) {
        for (const auto& behavior : root["behaviors"]) {
            doc.behaviors.push_back(parseBehavior(behavior));
        }
    }

    return doc;
}

}  // namespace bt
