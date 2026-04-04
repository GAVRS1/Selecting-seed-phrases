#pragma once

#include "bip39/wordlist.hpp"
#include "core/types.hpp"

namespace bip39 {

class MnemonicValidator {
public:
    explicit MnemonicValidator(const Wordlist& wordlist) : wordlist_(wordlist) {}

    bool is_valid_length(const core::Mnemonic& mnemonic) const;
    bool all_words_known(const core::Mnemonic& mnemonic) const;
    bool is_checksum_valid(const core::Mnemonic& mnemonic) const;
    bool validate(const core::Mnemonic& mnemonic) const;

private:
    const Wordlist& wordlist_;
};

} // namespace bip39
