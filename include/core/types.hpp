#pragma once

#include <cstdint>
#include <optional>
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
    std::vector<std::string> paths_ton;
    std::string target_addresses_path;
    std::string recovered_wallets_path{"recovered_wallets.txt"};
    std::string postgres_conninfo;
    std::string postgres_table{"recovered_wallets"};
    std::string env_file_path{".env"};
    std::string seen_mnemonics_path{"seen_mnemonics.txt"};
    std::string manual_wallets_path;
    std::string bip39_passphrase;
    bool shuffle_words{false};
    uint64_t shuffle_seed{0};
    uint32_t scan_limit{20};
    // 0 means "no artificial cap" (iterate until the generator exhausts variants
    // or pipeline stop condition triggers).
    uint64_t max_candidates{0};
    uint32_t threads{4};
};

} // namespace core
