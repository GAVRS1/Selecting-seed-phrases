#include "bip39/mnemonic_generator.hpp"

#include <algorithm>
#include <unordered_set>
#include <random>
#include <stdexcept>

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

    const std::uint64_t effective_seed =
        shuffle_seed_.has_value() ? *shuffle_seed_ : static_cast<std::uint64_t>(std::random_device{}());
    std::mt19937_64 seed_rng(effective_seed);

    for (std::size_t pos = 0; pos < current.size(); ++pos) {
        if (current[pos] != "*") {
            continue;
        }
        choices_by_position[pos] = allow_words_;
        std::shuffle(choices_by_position[pos].begin(), choices_by_position[pos].end(), seed_rng);
    }

    std::function<bool(std::size_t)> dfs = [&](std::size_t pos) {
        if (max_candidates > 0 && produced >= max_candidates) {
            return true;
        }

        if (pos == current.size()) {
            if (validator.validate(current)) {
                ++produced;
                return on_valid_candidate(current);
            }
            return false;
        }

        if (current[pos] != "*") {
            return dfs(pos + 1);
        }

        const auto& choices = choices_by_position[pos];
        for (const auto& candidate_word : choices) {
            current[pos] = candidate_word;
            if (dfs(pos + 1)) {
                return true;
            }
        }
        current[pos] = "*";
        return false;
    };

    dfs(0);
    return produced;
}

} // namespace bip39
