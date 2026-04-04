#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine {

class Matcher {
public:
    explicit Matcher(const std::string& target_addresses_file);
    explicit Matcher(std::unordered_set<std::string> target_addresses);
    Matcher(const Matcher&) = delete;
    Matcher& operator=(const Matcher&) = delete;
    Matcher(Matcher&& other) noexcept;
    Matcher& operator=(Matcher&& other) noexcept;

    std::optional<std::string> find_match(const std::vector<std::string>& addresses) const;
    bool contains(const std::vector<std::string>& addresses) const;
    bool has_targets() const;
    void stop();
    bool should_stop() const;

private:
    std::unordered_set<std::string> targets_;
    std::atomic<bool> stop_{false};
};

} // namespace engine
