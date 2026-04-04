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

bool Matcher::contains(const std::vector<std::string>& addresses) const {
    for (const auto& addr : addresses) {
        if (targets_.contains(addr)) {
            return true;
        }
    }
    return false;
}

void Matcher::stop() {
    stop_.store(true);
}

bool Matcher::should_stop() const {
    return stop_.load();
}

} // namespace engine
