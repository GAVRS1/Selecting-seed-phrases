#include "engine/matcher.hpp"

#include <fstream>
#include <stdexcept>

namespace engine {

Matcher::Matcher(const std::string& target_addresses_file) {
    std::ifstream input(target_addresses_file);
    if (!input) {
        throw std::runtime_error("Failed to open target addresses file: " + target_addresses_file);
    }
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            targets_.insert(line);
        }
    }
}

Matcher::Matcher(std::unordered_set<std::string> target_addresses)
    : targets_(std::move(target_addresses)) {}

Matcher::Matcher(Matcher&& other) noexcept
    : targets_(std::move(other.targets_)),
      stop_(other.stop_.load()) {}

Matcher& Matcher::operator=(Matcher&& other) noexcept {
    if (this != &other) {
        targets_ = std::move(other.targets_);
        stop_.store(other.stop_.load());
    }
    return *this;
}

std::optional<std::string> Matcher::find_match(const std::vector<std::string>& addresses) const {
    for (const auto& addr : addresses) {
        if (targets_.contains(addr)) {
            return addr;
        }
    }
    return std::nullopt;
}

bool Matcher::contains(const std::vector<std::string>& addresses) const {
    return find_match(addresses).has_value();
}

bool Matcher::contains_address(const std::string& address) const {
    return targets_.contains(address);
}

bool Matcher::has_targets() const {
    return !targets_.empty();
}

void Matcher::stop() {
    stop_.store(true);
}

bool Matcher::should_stop() const {
    return stop_.load();
}

} // namespace engine
