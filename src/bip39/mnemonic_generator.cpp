#include "bip39/mnemonic_generator.hpp"

#include <algorithm>
#include <unordered_set>
#include <random>
#include <stdexcept>

namespace bip39 {

MnemonicGenerator::MnemonicGenerator(const Wordlist& wordlist,
                                     std::vector<std::string> allow_words,
                                     std::optional<std::uint64_t> shuffle_seed)
    : wordlist_(wordlist), allow_words_(std::move(allow_words)) {
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
    if (shuffle_seed.has_value()) {
        std::mt19937_64 rng(*shuffle_seed);
        std::shuffle(allow_words_.begin(), allow_words_.end(), rng);
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

    std::function<bool(std::size_t)> dfs = [&](std::size_t pos) {
        if (max_candidates > 0 && produced >= max_candidates) {
            return true;
        }

        if (pos == current.size()) {
            std::unordered_set<std::string> unique_words;
            unique_words.reserve(current.size());
            for (const auto& word : current) {
                if (!unique_words.insert(word).second) {
                    return false;
                }
            }
            if (validator.validate(current)) {
                ++produced;
                return on_valid_candidate(current);
            }
            return false;
        }

        if (current[pos] != "*") {
            return dfs(pos + 1);
        }

        for (const auto& candidate_word : allow_words_) {
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
