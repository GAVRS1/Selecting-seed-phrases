#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"
#include "chains/bitcoin_module.hpp"
#include "core/types.hpp"
#include "engine/matcher.hpp"
#include "engine/pipeline.hpp"

#include <fstream>
#include <iostream>

int main() {
    std::ofstream wl("/tmp/test_wordlist_pipeline.txt");
    wl << "alpha\n" << "beta\n" << "gamma\n";
    wl.close();

    bip39::Wordlist wordlist("/tmp/test_wordlist_pipeline.txt");
    bip39::MnemonicValidator validator(wordlist);
    bip39::MnemonicGenerator generator(wordlist, {"alpha", "beta"});

    core::AppConfig cfg;
    cfg.template_words = core::Mnemonic(12, "alpha");
    cfg.paths_btc = {"m/84'/0'/0'/0/{i}"};
    cfg.scan_limit = 1;
    cfg.max_candidates = 1;
    cfg.threads = 1;

    engine::Matcher matcher(std::unordered_set<std::string>{"non-existing"});
    engine::Pipeline pipeline(cfg, validator, generator, matcher);
    pipeline.register_chain(std::make_unique<chains::BitcoinModule>());
    pipeline.run();

    std::cout << "test_pipeline passed\n";
    return 0;
}
