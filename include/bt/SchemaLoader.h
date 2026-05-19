#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include "bt/Blackboard.h"
#include "bt/BehaviorTree.h"
#include "bt/PartitionConfig.h"
#include "bt/SchemaNode.h"
#include "bt/Status.h"

// Forward declaration to avoid pulling in RuntimeRegistry.h everywhere.
namespace bt { class RuntimeRegistry; }

namespace bt {

class SchemaLoadError : public std::runtime_error {
public:
    explicit SchemaLoadError(std::string_view msg)
        : std::runtime_error(std::string(msg)) {}
};

class SchemaCycleError : public std::runtime_error {
public:
    explicit SchemaCycleError(std::string_view msg)
        : std::runtime_error(std::string(msg)) {}
};

struct LoaderRegistry {
    std::unordered_map<std::string, std::function<Status()>> actions;
    std::unordered_map<std::string, std::function<bool()>> conditions;
};

class SchemaManifest {
public:
    void add(std::string_view name, std::string_view yaml);
    [[nodiscard]] bool has(std::string_view name) const noexcept;
    [[nodiscard]] std::string_view get(std::string_view name) const;

private:
    std::unordered_map<std::string, std::string> entries_;
};

class SchemaLoader {
public:
    [[nodiscard]] static BehaviorTree load(std::string_view yaml, const LoaderRegistry& reg);
    [[nodiscard]] static BehaviorTree load(std::string_view yaml, const RuntimeRegistry& reg,
                                            Blackboard blackboard = {});
    [[nodiscard]] static BehaviorTree load(std::string_view yaml, const RuntimeRegistry& reg,
                                            Blackboard blackboard,
                                            const PartitionConfig& partition);

    // Build directly from an already-parsed SchemaDoc. Used by PathExplorer
    // so it can build multiple trees from the same doc without re-parsing.
    [[nodiscard]] static BehaviorTree load(const SchemaDoc& doc, const LoaderRegistry& reg,
                                            Blackboard blackboard = {});
    [[nodiscard]] static BehaviorTree load(const SchemaDoc& doc, const LoaderRegistry& reg,
                                            Blackboard blackboard,
                                            const PartitionConfig& partition);

    [[nodiscard]] static BehaviorTree loadWithManifest(std::string_view yaml,
                                                        const SchemaManifest& manifest,
                                                        const LoaderRegistry& reg);
};

}  // namespace bt
