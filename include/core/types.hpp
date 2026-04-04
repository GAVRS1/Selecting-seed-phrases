#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core {

using Word = std::string;
using Mnemonic = std::vector<Word>;
using Address = std::string;
using PathList = std::vector<std::string>;

struct AppConfig {
    std::string wordlist_path{"data/bip39_english.txt"};
    std::vector<std::string> template_words;
    std::vector<std::string> allow_words;
    std::vector<std::string> chains;
    std::vector<std::string> paths_btc;
    std::vector<std::string> paths_eth;
    std::vector<std::string> paths_sol;
    std::string target_addresses_path;
    std::string recovered_wallets_path{"recovered_wallets.txt"};
    std::string bip39_passphrase;
    uint32_t scan_limit{20};
    uint64_t max_candidates{100000};
    uint32_t threads{4};
};

} // namespace core
