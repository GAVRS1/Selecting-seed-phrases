#include "bip39/mnemonic_generator.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <stdexcept>
#include <unordered_set>

namespace bip39 {

MnemonicGenerator::MnemonicGenerator(const Wordlist& wordlist,
                                     std::vector<std::string> allow_words,
                                     std::optional<std::uint64_t> shuffle_seed)
    : wordlist_(wordlist), allow_words_(std::move(allow_words)), shuffle_seed_(shuffle_seed) {
    if (allow_words_.empty()) {
        allow_words_ = wordlist_.words();
    } else {
        std::unordered_set<std::string> unique_allow;
        const auto all_words = wordlist_.words();
        std::unordered_set<std::string> dictionary(all_words.begin(), all_words.end());

        std::vector<std::string> filtered_allow;
        filtered_allow.reserve(allow_words_.size());
        for (const auto& word : allow_words_) {
            if (!dictionary.contains(word)) {
                continue;
            }
            if (unique_allow.insert(word).second) {
                filtered_allow.push_back(word);
            }
        }
        if (filtered_allow.empty()) {
            throw std::invalid_argument("No valid allow words from BIP39 dictionary");
        }
        allow_words_ = std::move(filtered_allow);
    }
}

std::size_t MnemonicGenerator::generate(
    const core::Mnemonic& pattern,
    const MnemonicValidator& validator,
    std::uint64_t max_candidates,
    const std::function<bool(const core::Mnemonic&)>& on_valid_candidate) const {

    if (pattern.empty()) {
        throw std::invalid_argument("Mnemonic pattern cannot be empty");
    }

    std::size_t produced = 0;
    core::Mnemonic current = pattern;
    std::vector<std::vector<std::string>> choices_by_position(current.size());
    std::vector<std::size_t> wildcard_positions;

    const std::uint64_t effective_seed =
        shuffle_seed_.has_value() ? *shuffle_seed_ : static_cast<std::uint64_t>(std::random_device{}());
    std::mt19937_64 seed_rng(effective_seed);

    for (std::size_t pos = 0; pos < current.size(); ++pos) {
        if (current[pos] != "*") {
            continue;
        }
        wildcard_positions.push_back(pos);
        choices_by_position[pos] = allow_words_;
        std::shuffle(choices_by_position[pos].begin(), choices_by_position[pos].end(), seed_rng);
    }

    if (wildcard_positions.empty()) {
        if (validator.validate(current)) {
            ++produced;
            on_valid_candidate(current);
        }
        return produced;
    }

    // Shuffle wildcard traversal order so consecutive candidates do not only vary
    // near the tail of the mnemonic.
    std::vector<std::size_t> traversal_positions = wildcard_positions;
    std::shuffle(traversal_positions.begin(), traversal_positions.end(), seed_rng);

    const std::size_t base = allow_words_.size();
    if (base == 1) {
        for (const std::size_t pos : traversal_positions) {
            current[pos] = choices_by_position[pos][0];
        }
        if (validator.validate(current)) {
            ++produced;
            on_valid_candidate(current);
        }
        return produced;
    }

    const auto random_digit = [&](std::size_t upper_exclusive) -> std::size_t {
        std::uniform_int_distribution<std::size_t> dist(0, upper_exclusive - 1);
        return dist(seed_rng);
    };

    std::vector<std::size_t> start_digits(traversal_positions.size(), 0);
    std::vector<std::size_t> index_digits(traversal_positions.size(), 0);
    std::vector<std::size_t> step_digits(traversal_positions.size(), 0);
    for (std::size_t i = 0; i < traversal_positions.size(); ++i) {
        start_digits[i] = random_digit(base);
    }
    index_digits = start_digits;

    do {
        for (std::size_t i = 0; i < traversal_positions.size(); ++i) {
            step_digits[i] = random_digit(base);
        }
    } while (std::all_of(step_digits.begin(), step_digits.end(), [](std::size_t value) { return value == 0; }) ||
             std::gcd(step_digits[0], base) != 1);

    bool completed_full_cycle = false;
    while (!completed_full_cycle) {
        if (max_candidates > 0 && produced >= max_candidates) {
            break;
        }

        for (std::size_t i = 0; i < traversal_positions.size(); ++i) {
            const std::size_t pos = traversal_positions[i];
            const std::size_t choice_index = index_digits[i];
            current[pos] = choices_by_position[pos][choice_index];
        }

        if (validator.validate(current)) {
            ++produced;
            if (on_valid_candidate(current)) {
                break;
            }
        }

        std::size_t carry = 0;
        for (std::size_t i = 0; i < index_digits.size(); ++i) {
            const std::size_t sum = index_digits[i] + step_digits[i] + carry;
            index_digits[i] = sum % base;
            carry = sum / base;
        }
        completed_full_cycle = (index_digits == start_digits);
    }

    return produced;
}

} // namespace bip39
