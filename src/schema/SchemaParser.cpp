#include "bt/SchemaParser.h"

#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <memory>
#include <string>

namespace bt {

namespace {

struct ThrowingRyml {
    ryml::Callbacks prev_;
    ThrowingRyml() : prev_(ryml::get_callbacks()) {
        ryml::Callbacks cb = prev_;
        cb.m_error_basic = [](ryml::csubstr msg, ryml::ErrorDataBasic const&, void*) {
            throw SchemaParseError(std::string(msg.data(), msg.len));
        };
        cb.m_error_parse = [](ryml::csubstr msg, ryml::ErrorDataParse const&, void*) {
            throw SchemaParseError(std::string(msg.data(), msg.len));
        };
        ryml::set_callbacks(cb);
    }
    ~ThrowingRyml() { ryml::set_callbacks(prev_); }
};

static std::string rval(ryml::ConstNodeRef n) { return {n.val().data(), n.val().len}; }
static bool has(ryml::ConstNodeRef n, const char* k) { return n.is_map() && n.has_child(k); }

std::unique_ptr<SchemaNode> parseNode(ryml::ConstNodeRef node);

void parseChildren(ryml::ConstNodeRef node, SchemaNode& out) {
    if (!has(node, "children")) return;
    for (ryml::ConstNodeRef child : node["children"])
        out.children.push_back(parseNode(child));
}

std::unique_ptr<SchemaNode> parseLeaf(SchemaNodeType type, ryml::ConstNodeRef node) {
    if (!has(node, "name"))
        throw SchemaParseError("leaf node missing required field 'name'");
    auto sn = std::make_unique<SchemaNode>();
    sn->type = type;
    sn->name = rval(node["name"]);
    return sn;
}

std::unique_ptr<SchemaNode> parseParallel(ryml::ConstNodeRef node) {
    auto sn = std::make_unique<SchemaNode>();
    sn->type = SchemaNodeType::PARALLEL;
    if (has(node, "policy")) {
        auto p = rval(node["policy"]);
        if (p == "all") {
            sn->policy = SchemaPolicy::ALL;
        } else if (p == "any") {
            sn->policy = SchemaPolicy::ANY;
        } else if (p == "threshold") {
            sn->policy = SchemaPolicy::THRESHOLD;
            if (has(node, "threshold"))
                node["threshold"] >> sn->threshold;
        } else {
            throw SchemaParseError("unknown parallel policy: " + p);
        }
    }
    parseChildren(node, *sn);
    return sn;
}

std::unique_ptr<SchemaNode> parseNode(ryml::ConstNodeRef node) {
    if (!has(node, "type"))
        throw SchemaParseError("node missing required field 'type'");
    auto t = rval(node["type"]);
    std::unique_ptr<SchemaNode> sn;
    if (t == "action") {
        sn = parseLeaf(SchemaNodeType::ACTION, node);
    } else if (t == "condition") {
        sn = parseLeaf(SchemaNodeType::CONDITION, node);
    } else if (t == "sequence") {
        sn = std::make_unique<SchemaNode>();
        sn->type = SchemaNodeType::SEQUENCE;
        if (has(node, "name")) sn->name = rval(node["name"]);
        parseChildren(node, *sn);
    } else if (t == "selector") {
        sn = std::make_unique<SchemaNode>();
        sn->type = SchemaNodeType::SELECTOR;
        if (has(node, "name")) sn->name = rval(node["name"]);
        parseChildren(node, *sn);
    } else if (t == "parallel") {
        sn = parseParallel(node);
    } else {
        throw SchemaParseError("unknown node type: " + t);
    }
    if (has(node, "intent")) sn->intent = rval(node["intent"]);
    return sn;
}

BehaviorSchema parseBehavior(ryml::ConstNodeRef node) {
    if (!has(node, "name"))
        throw SchemaParseError("behavior missing required field 'name'");
    BehaviorSchema b;
    b.name = rval(node["name"]);
    if (has(node, "when"))          b.condition     = rval(node["when"]);
    if (has(node, "intent"))        b.intent        = rval(node["intent"]);
    if (has(node, "interruptible")) node["interruptible"] >> b.interruptible;
    if (has(node, "tree"))          b.tree          = parseNode(node["tree"]);
    return b;
}

}  // namespace

SchemaDoc SchemaParser::parse(std::string_view yaml) {
    ThrowingRyml guard;
    ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(yaml));

    ryml::ConstNodeRef root = tree.rootref();
    if (!has(root, "schema_version"))
        throw SchemaParseError("missing required field 'schema_version'");

    SchemaDoc doc;
    doc.schemaVersion = rval(root["schema_version"]);
    if (has(root, "subtree")) doc.subtreeName = rval(root["subtree"]);

    if (has(root, "import")) {
        for (ryml::ConstNodeRef imp : root["import"])
            doc.imports.push_back(rval(imp));
    }
    if (has(root, "state")) {
        for (ryml::ConstNodeRef e : root["state"]) {
            StateDeclaration decl;
            decl.key  = rval(e["key"]);
            decl.type = rval(e["type"]);
            doc.stateDeclarations.push_back(std::move(decl));
        }
    }
    if (has(root, "behaviors")) {
        for (ryml::ConstNodeRef b : root["behaviors"])
            doc.behaviors.push_back(parseBehavior(b));
    }
    return doc;
}

}  // namespace bt
