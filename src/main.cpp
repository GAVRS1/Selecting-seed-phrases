#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"
#include "chains/bitcoin_module.hpp"
#include "chains/ethereum_module.hpp"
#include "chains/solana_module.hpp"
#include "cli/args.hpp"
#include "engine/matcher.hpp"
#include "engine/pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>

namespace {
bool chain_enabled(const core::AppConfig& cfg, const std::string& chain_name) {
    if (cfg.chains.empty()) {
        return true;
    }
    return std::find(cfg.chains.begin(), cfg.chains.end(), chain_name) != cfg.chains.end();
}
} // namespace

int main(int argc, char** argv) {
    try {
        auto cfg = cli::parse_args(argc, argv);

        bip39::Wordlist wl(cfg.wordlist_path);
        if (!wl.has_full_bip39_english_size()) {
            std::cerr
                << "Warning: loaded " << wl.words().size()
                << " words instead of 2048; strict BIP39 validation (dictionary + checksum) is disabled. "
                << "The file is treated as wildcard candidate pool.\n";
        }
        bip39::MnemonicValidator validator(wl);
        std::optional<std::uint64_t> shuffle_seed;
        if (cfg.shuffle_words) {
            if (cfg.shuffle_seed != 0) {
                shuffle_seed = cfg.shuffle_seed;
            } else {
                shuffle_seed = static_cast<std::uint64_t>(std::random_device{}());
            }
        }
        bip39::MnemonicGenerator generator(wl, cfg.allow_words, shuffle_seed);

        engine::Matcher matcher = cfg.target_addresses_path.empty()
                                      ? engine::Matcher(std::unordered_set<std::string>{})
                                      : engine::Matcher(cfg.target_addresses_path);
        engine::Pipeline pipeline(cfg, validator, generator, matcher);

        if (chain_enabled(cfg, "btc")) {
            pipeline.register_chain(std::make_unique<chains::BitcoinModule>());
        }
        if (chain_enabled(cfg, "eth")) {
            pipeline.register_chain(std::make_unique<chains::EthereumModule>());
        }
        if (chain_enabled(cfg, "sol")) {
            pipeline.register_chain(std::make_unique<chains::SolanaModule>());
        }

        pipeline.run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
