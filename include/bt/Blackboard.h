#pragma once

#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace bt {

// Key-value store for runtime state. Values are either set directly or
// backed by registered lambdas that are pulled on refresh().
// BehaviorTree calls refresh() before each tick so nodes always see
// up-to-date state without querying the engine themselves.
class Blackboard {
public:
    template <typename T>
    void set(const std::string& key, T value) {
        values_[key] = std::move(value);
    }

    template <typename T>
    void registerSource(const std::string& key, std::function<T()> source) {
        sources_[key] = [src = std::move(source)]() -> std::any { return src(); };
    }

    template <typename T>
    [[nodiscard]] T get(const std::string& key) const {
        auto valueIt = values_.find(key);
        if (valueIt != values_.end()) {
            return std::any_cast<T>(valueIt->second);
        }
        auto sourceIt = sources_.find(key);
        if (sourceIt != sources_.end()) {
            return std::any_cast<T>(sourceIt->second());
        }
        throw std::out_of_range("Blackboard key not found: " + key);
    }

    [[nodiscard]] bool has(const std::string& key) const noexcept {
        return values_.contains(key) || sources_.contains(key);
    }

    // Pulls all registered sources into the value store.
    // Called by BehaviorTree before each tick.
    void refresh() {
        for (const auto& [key, source] : sources_) {
            values_[key] = source();
        }
    }

    [[nodiscard]] const std::unordered_map<std::string, std::any>& values() const noexcept {
        return values_;
    }

private:
    std::unordered_map<std::string, std::any> values_;
    std::unordered_map<std::string, std::function<std::any()>> sources_;
};

}  // namespace bt
