#pragma once

#include <string>

#include "bt/RegistryStore.h"

namespace bt {

// Generates a YAML registry catalog from a RegistryStore.
// This is not a behavior tree structure — it is a machine-readable
// description of all registered actions, conditions, and state keys
// with their declared dependencies. Used by editors and validators.
class SchemaSerializer {
public:
    [[nodiscard]] static std::string toYaml(const RegistryStore& store);
};

}  // namespace bt
