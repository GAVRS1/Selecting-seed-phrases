#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"

#include <cassert>
#include <fstream>
#include <iostream>

int main() {
    const std::string custom_wordlist_path = "/tmp/test_generator_wordlist.txt";
    {
        std::ofstream custom_wl(custom_wordlist_path);
        custom_wl << "abandon\nabout\nability\n";
    }

    bip39::Wordlist wordlist(custom_wordlist_path);
    bip39::MnemonicValidator validator(wordlist);
    bip39::MnemonicGenerator generator(wordlist, {"abandon"});

    const core::Mnemonic template_words(12, "*");
    std::size_t seen = 0;

    const std::size_t produced = generator.generate(
        template_words,
        validator,
        1,
        [&](const core::Mnemonic& candidate) {
            ++seen;
            for (const auto& word : candidate) {
                assert(word == "abandon");
            }
            return true;
        });

    assert(produced == 1);
    assert(seen == 1);

    std::cout << "test_mnemonic_generator passed\n";
    return 0;
}
