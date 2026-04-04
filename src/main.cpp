#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"
#include "chains/bitcoin_module.hpp"
#include "chains/ethereum_module.hpp"
#include "chains/solana_module.hpp"
#include "cli/args.hpp"
#include "engine/matcher.hpp"
#include "engine/pipeline.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        auto cfg = cli::parse_args(argc, argv);

        bip39::Wordlist wl(cfg.wordlist_path);
        bip39::MnemonicValidator validator(wl);
        bip39::MnemonicGenerator generator(wl, cfg.allow_words);

        engine::Matcher matcher(cfg.target_addresses_path);
        engine::Pipeline pipeline(cfg, validator, generator, matcher);

        pipeline.register_chain(std::make_unique<chains::BitcoinModule>());
        pipeline.register_chain(std::make_unique<chains::EthereumModule>());
        pipeline.register_chain(std::make_unique<chains::SolanaModule>());

        pipeline.run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
