#include "bip39/mnemonic_generator.hpp"

#include <stdexcept>

namespace bip39 {

MnemonicGenerator::MnemonicGenerator(const Wordlist& wordlist, std::vector<std::string> allow_words)
    : wordlist_(wordlist), allow_words_(std::move(allow_words)) {
    if (allow_words_.empty()) {
        allow_words_ = wordlist_.words();
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
