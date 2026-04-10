#include "engine/matcher.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace engine {
namespace {

std::string trim_copy(const std::string& value) {
    const auto begin =
        std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto end =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

bool is_hex_64(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<std::string> normalize_ton_raw(const std::string& value) {
    const auto pos = value.find(':');
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    const std::string workchain_text = trim_copy(value.substr(0, pos));
    const std::string hash_text = trim_copy(value.substr(pos + 1));
    if (workchain_text.empty() || !is_hex_64(hash_text)) {
        return std::nullopt;
    }

    std::size_t parsed_chars = 0;
    int workchain = 0;
    try {
        workchain = std::stoi(workchain_text, &parsed_chars, 10);
    } catch (...) {
        return std::nullopt;
    }
    if (parsed_chars != workchain_text.size()) {
        return std::nullopt;
    }
    return std::to_string(workchain) + ":" + to_lower_ascii(hash_text);
}

std::optional<std::vector<std::uint8_t>> decode_base64(const std::string& encoded) {
    static const std::array<int, 256> decode_table = [] {
        std::array<int, 256> table{};
        table.fill(-1);
        for (int i = 0; i < 26; ++i) {
            table[static_cast<std::size_t>('A' + i)] = i;
            table[static_cast<std::size_t>('a' + i)] = 26 + i;
        }
        for (int i = 0; i < 10; ++i) {
            table[static_cast<std::size_t>('0' + i)] = 52 + i;
        }
        table[static_cast<std::size_t>('+')] = 62;
        table[static_cast<std::size_t>('/')] = 63;
        table[static_cast<std::size_t>('=')] = 0;
        return table;
    }();

    if (encoded.empty() || (encoded.size() % 4) != 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out;
    out.reserve((encoded.size() / 4) * 3);
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const char c0 = encoded[i];
        const char c1 = encoded[i + 1];
        const char c2 = encoded[i + 2];
        const char c3 = encoded[i + 3];
        const int v0 = decode_table[static_cast<std::uint8_t>(c0)];
        const int v1 = decode_table[static_cast<std::uint8_t>(c1)];
        const int v2 = decode_table[static_cast<std::uint8_t>(c2)];
        const int v3 = decode_table[static_cast<std::uint8_t>(c3)];
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
            return std::nullopt;
        }

        const std::uint32_t triple =
            (static_cast<std::uint32_t>(v0) << 18) | (static_cast<std::uint32_t>(v1) << 12) |
            (static_cast<std::uint32_t>(v2) << 6) | static_cast<std::uint32_t>(v3);

        out.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xff));
        if (c2 != '=') {
            out.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xff));
        }
        if (c3 != '=') {
            out.push_back(static_cast<std::uint8_t>(triple & 0xff));
        }
    }
    return out;
}

std::optional<std::string> normalize_ton_user_friendly(const std::string& value) {
    if (value.size() != 48) {
        return std::nullopt;
    }
    std::string base64 = value;
    std::replace(base64.begin(), base64.end(), '-', '+');
    std::replace(base64.begin(), base64.end(), '_', '/');

    const auto decoded = decode_base64(base64);
    if (!decoded.has_value() || decoded->size() != 36) {
        return std::nullopt;
    }

    const std::int8_t wc8 = static_cast<std::int8_t>((*decoded)[1]);
    const int workchain = static_cast<int>(wc8);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string raw = std::to_string(workchain) + ":";
    raw.reserve(66);
    for (std::size_t i = 2; i < 34; ++i) {
        const auto byte = (*decoded)[i];
        raw.push_back(kHex[(byte >> 4) & 0x0f]);
        raw.push_back(kHex[byte & 0x0f]);
    }
    return raw;
}

std::optional<std::string> normalize_possible_ton_address(const std::string& value) {
    if (const auto raw = normalize_ton_raw(value); raw.has_value()) {
        return raw;
    }
    return normalize_ton_user_friendly(value);
}

} // namespace

Matcher::Matcher(const std::string& target_addresses_file) {
    std::ifstream input(target_addresses_file);
    if (!input) {
        throw std::runtime_error("Failed to open target addresses file: " + target_addresses_file);
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string clean = trim_copy(line);
        if (clean.empty()) {
            continue;
        }
        targets_.insert(clean);
        if (const auto normalized = normalize_possible_ton_address(clean); normalized.has_value()) {
            targets_.insert(*normalized);
        }
    }
}

Matcher::Matcher(std::unordered_set<std::string> target_addresses)
    : targets_(std::move(target_addresses)) {
    std::vector<std::string> to_add;
    to_add.reserve(targets_.size());
    for (const auto& value : targets_) {
        if (const auto normalized = normalize_possible_ton_address(value); normalized.has_value()) {
            to_add.push_back(*normalized);
        }
    }
    for (const auto& value : to_add) {
        targets_.insert(value);
    }
}

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
    if (targets_.contains(address)) {
        return true;
    }
    if (const auto normalized = normalize_possible_ton_address(address); normalized.has_value()) {
        return targets_.contains(*normalized);
    }
    return false;
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
