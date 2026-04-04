#pragma once

#include <atomic>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine {

class Matcher {
public:
    explicit Matcher(const std::string& target_addresses_file);
    explicit Matcher(std::unordered_set<std::string> target_addresses);

    bool contains(const std::vector<std::string>& addresses) const;
    void stop();
    bool should_stop() const;

private:
    std::unordered_set<std::string> targets_;
    std::atomic<bool> stop_{false};
};

} // namespace engine
