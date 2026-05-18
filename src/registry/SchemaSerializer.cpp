#include "bt/SchemaSerializer.h"

#include <sstream>

namespace bt {

namespace {

void appendList(std::ostringstream& out, const std::vector<std::string>& items,
                std::string_view indent) {
    for (const auto& item : items) {
        out << indent << "- " << item << "\n";
    }
}

}  // namespace

std::string SchemaSerializer::toYaml(const RegistryStore& store) {
    std::ostringstream out;
    out << "schema_version: \"1.0\"\n";

    auto stateKeys = store.allStateKeys();
    if (!stateKeys.empty()) {
        out << "\nstate:\n";
        for (const auto& key : stateKeys) {
            out << "  - key: " << key.key << "\n";
            out << "    type: " << key.type << "\n";
        }
    }

    auto actions = store.allActions();
    if (!actions.empty()) {
        out << "\nactions:\n";
        for (const auto& action : actions) {
            out << "  - name: " << action.name << "\n";
            if (!action.intent.empty()) {
                out << "    intent: \"" << action.intent << "\"\n";
            }
            if (!action.reads.empty()) {
                out << "    reads:\n";
                appendList(out, action.reads, "      ");
            }
            if (!action.writes.empty()) {
                out << "    writes:\n";
                appendList(out, action.writes, "      ");
            }
        }
    }

    auto conditions = store.allConditions();
    if (!conditions.empty()) {
        out << "\nconditions:\n";
        for (const auto& cond : conditions) {
            out << "  - name: " << cond.name << "\n";
            if (!cond.intent.empty()) {
                out << "    intent: \"" << cond.intent << "\"\n";
            }
            if (!cond.reads.empty()) {
                out << "    reads:\n";
                appendList(out, cond.reads, "      ");
            }
        }
    }

    return out.str();
}

}  // namespace bt
