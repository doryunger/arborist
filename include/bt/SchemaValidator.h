#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "bt/SchemaNode.h"

namespace bt {

class SchemaIssue {
public:
    enum class Severity : std::uint8_t { kError, kWarning };

    SchemaIssue(Severity severity, std::string message)
        : severity_(severity), message_(std::move(message)) {}

    [[nodiscard]] bool isError() const noexcept { return severity_ == Severity::kError; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    Severity severity_;
    std::string message_;
};

struct SchemaRegistry {
    std::unordered_set<std::string> conditions;
    std::unordered_set<std::string> actions;
};

class SchemaValidator {
public:
    [[nodiscard]] static std::vector<SchemaIssue> validate(const SchemaDoc& doc);
    [[nodiscard]] static std::vector<SchemaIssue> validate(const SchemaDoc& doc,
                                                            const SchemaRegistry& registry);
};

}  // namespace bt
