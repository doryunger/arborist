#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "bt/SchemaNode.h"

namespace bt {

class SchemaParseError : public std::runtime_error {
public:
    explicit SchemaParseError(std::string_view msg)
        : std::runtime_error(std::string(msg)) {}
};

class SchemaParser {
public:
    [[nodiscard]] static SchemaDoc parse(std::string_view yaml);
};

}  // namespace bt
