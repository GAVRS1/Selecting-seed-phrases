#include "bip39/mnemonic_validator.hpp"

#include <array>
#include <set>

namespace bip39 {

bool MnemonicValidator::is_valid_length(const core::Mnemonic& mnemonic) const {
    static const std::set<std::size_t> lengths{12, 15, 18, 21, 24};
    return lengths.contains(mnemonic.size());
}

bool MnemonicValidator::all_words_known(const core::Mnemonic& mnemonic) const {
    for (const auto& word : mnemonic) {
        if (!wordlist_.contains(word)) {
            return false;
        }
    }
    return true;
}

bool MnemonicValidator::is_checksum_valid(const core::Mnemonic& mnemonic) const {
    // Skeleton-level check: production implementation should use BIP-39 checksum bit validation.
    // Here we keep strict filtering by requiring all words to map and a deterministic parity check.
    int acc = 0;
    for (const auto& word : mnemonic) {
        const int idx = wordlist_.index_of(word);
        if (idx < 0) {
            return false;
        }
        acc ^= idx;
    }
    return (acc % 2) == 0;
}

bool MnemonicValidator::validate(const core::Mnemonic& mnemonic) const {
    return is_valid_length(mnemonic) && all_words_known(mnemonic) && is_checksum_valid(mnemonic);
}

} // namespace bip39
