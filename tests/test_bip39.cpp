#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"

#include <cassert>
#include <fstream>
#include <iostream>

int main() {
    std::ofstream wl("/tmp/test_wordlist.txt");
    wl << "alpha\n" << "beta\n" << "gamma\n";
    wl.close();

    bip39::Wordlist wordlist("/tmp/test_wordlist.txt");
    bip39::MnemonicValidator validator(wordlist);

    core::Mnemonic mnemonic(12, "alpha");
    assert(validator.is_valid_length(mnemonic));
    assert(validator.all_words_known(mnemonic));

    std::cout << "test_bip39 passed\n";
    return 0;
}
