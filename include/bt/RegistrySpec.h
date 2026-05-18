#pragma once

#include <string>
#include <vector>

namespace bt {

struct ActionSpec {
    std::string name;
    std::string intent;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
};

struct ConditionSpec {
    std::string name;
    std::string intent;
    std::vector<std::string> reads;
};

struct StateKeySpec {
    std::string key;
    std::string type;
};

}  // namespace bt
