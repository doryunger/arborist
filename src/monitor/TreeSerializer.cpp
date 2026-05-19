#include "bt/TreeSerializer.h"

#include <span>
#include <string>

namespace bt {

namespace {

void appendEscaped(std::string& out, std::string_view text) {
    for (char character : text) {
        switch (character) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += character; break;
        }
    }
}

void serializeNode(std::string& out, const Node& node) {
    out += R"({"name":")";
    appendEscaped(out, node.name());
    out += R"(","type":")";
    appendEscaped(out, node.typeName());
    out += R"(","children":[)";

    bool isFirst = true;
    for (const auto& child : node.children()) {
        if (!isFirst) { out += ','; }
        serializeNode(out, *child);
        isFirst = false;
    }

    out += "]}";
}

}  // namespace

std::string TreeSerializer::toJson(const Node& root) {
    std::string out;
    out.reserve(256);
    serializeNode(out, root);
    return out;
}

}  // namespace bt
