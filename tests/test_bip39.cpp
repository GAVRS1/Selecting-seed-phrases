#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

std::string resolve_wordlist_path() {
    const std::filesystem::path direct{"data/bip39_english.txt"};
    if (std::filesystem::exists(direct)) {
        return direct.string();
    }

    const std::filesystem::path parent{"../data/bip39_english.txt"};
    if (std::filesystem::exists(parent)) {
        return parent.string();
    }

    throw std::runtime_error("Unable to locate data/bip39_english.txt");
}

} // namespace

int main() {
    bip39::Wordlist wordlist(resolve_wordlist_path());
    bip39::MnemonicValidator validator(wordlist);

    const core::Mnemonic valid{
        "abandon", "abandon", "abandon", "abandon", "abandon", "abandon",
        "abandon", "abandon", "abandon", "abandon", "abandon", "about",
    };

    const core::Mnemonic invalid_checksum{
        "abandon", "abandon", "abandon", "abandon", "abandon", "abandon",
        "abandon", "abandon", "abandon", "abandon", "abandon", "ability",
    };

    assert(validator.is_valid_length(valid));
    assert(validator.all_words_known(valid));
    assert(validator.is_checksum_valid(valid));
    assert(validator.validate(valid));

    assert(validator.all_words_known(invalid_checksum));
    assert(!validator.is_checksum_valid(invalid_checksum));
    assert(!validator.validate(invalid_checksum));

    std::cout << "test_bip39 passed\n";
    return 0;
}
