#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"
#include "chains/bitcoin_module.hpp"
#include "core/types.hpp"
#include "engine/matcher.hpp"
#include "engine/pipeline.hpp"

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
    bip39::MnemonicGenerator generator(wordlist, {"abandon", "about"});

    core::AppConfig cfg;
    cfg.template_words = {
        "abandon", "abandon", "abandon", "abandon", "abandon", "abandon",
        "abandon", "abandon", "abandon", "abandon", "abandon", "about",
    };
    cfg.paths_btc = {"m/84'/0'/0'/0/{i}"};
    cfg.scan_limit = 1;
    cfg.max_candidates = 0;
    cfg.threads = 1;

    engine::Matcher matcher(std::unordered_set<std::string>{"non-existing"});
    engine::Pipeline pipeline(cfg, validator, generator, matcher);
    pipeline.register_chain(std::make_unique<chains::BitcoinModule>());
    pipeline.run();

    std::cout << "test_pipeline passed\n";
    return 0;
}
