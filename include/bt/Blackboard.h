#pragma once

#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace bt {

// Key-value store for runtime state. Values are either set directly or
// backed by registered lambdas that are pulled on refresh().
// BehaviorTree calls refresh() before each tick so nodes always see
// up-to-date state without querying the engine themselves.
//
// Type safety: once a key is written via set<T> or registerSource<T>, the
// type T is recorded. Any subsequent set/registerSource with a different type
// throws std::runtime_error. get<T> with a mismatched type also throws
// std::runtime_error (instead of std::bad_any_cast) with the key name and
// conflicting types included in the message.
class Blackboard {
public:
    template <typename T>
    void set(const std::string& key, T value) {
        enforceType<T>(key);
        values_[key] = std::move(value);
    }

    template <typename T>
    void registerSource(const std::string& key, std::function<T()> source) {
        enforceType<T>(key);
        sources_[key] = [src = std::move(source)]() -> std::any { return src(); };
    }

    template <typename T>
    [[nodiscard]] T get(const std::string& key) const {
        const auto regIt = typeRegistry_.find(key);
        if (regIt != typeRegistry_.end() &&
            regIt->second != std::type_index(typeid(T))) {
            throw std::runtime_error(
                "Blackboard type mismatch for key '" + key +
                "': declared " + regIt->second.name() +
                " but requested " + typeid(T).name());
        }
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
    template <typename T>
    void enforceType(const std::string& key) {
        const std::type_index tid(typeid(T));
        auto [it, inserted] = typeRegistry_.emplace(key, tid);
        if (!inserted && it->second != tid) {
            throw std::runtime_error(
                "Blackboard type conflict for key '" + key +
                "': previously " + it->second.name() +
                ", now " + tid.name());
        }
    }

    std::unordered_map<std::string, std::any>               values_;
    std::unordered_map<std::string, std::function<std::any()>> sources_;
    std::unordered_map<std::string, std::type_index>         typeRegistry_;
};

}  // namespace bt
