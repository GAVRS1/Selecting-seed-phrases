#pragma once

#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "chains/i_chain_module.hpp"
#include "core/types.hpp"
#include "engine/matcher.hpp"

#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine {

class Pipeline {
public:
    Pipeline(const core::AppConfig& config,
             const bip39::MnemonicValidator& validator,
             const bip39::MnemonicGenerator& generator,
             Matcher& matcher);

    void register_chain(std::unique_ptr<chains::IChainModule> module);
    void run();

private:
    core::AppConfig config_;
    const bip39::MnemonicValidator& validator_;
    const bip39::MnemonicGenerator& generator_;
    Matcher& matcher_;
    std::vector<std::unique_ptr<chains::IChainModule>> modules_;
    std::unordered_set<std::string> recovered_chains_;
    std::mutex recovered_mutex_;
    std::mutex console_mutex_;
    std::atomic<bool> console_header_printed_{false};

    bool is_chain_recovered(const std::string& chain_name);
    void mark_chain_recovered(const std::string& chain_name);
    void print_console_header();
    void print_console_row(const std::string& chain_name,
                           double balance_coin,
                           const std::string& address,
                           const std::string& mnemonic_words);
    void persist_recovered_wallet(const std::string& chain_name,
                                  const std::string& address,
                                  const core::Mnemonic& mnemonic,
                                  double balance_coin,
                                  const std::string& coin_ticker) const;
};

} // namespace engine
