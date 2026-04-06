#pragma once

#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "chains/i_chain_module.hpp"
#include "core/secure_buffer.hpp"
#include "core/types.hpp"
#include "engine/matcher.hpp"

#include <filesystem>
#include <memory>
#include <atomic>
#include <optional>
#include <mutex>
#include <string>
#include <unordered_set>
#include <tuple>
#include <utility>
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
    std::mutex queue_file_mutex_;
    std::filesystem::path queue_file_path_;

    bool is_chain_recovered(const std::string& chain_name);
    void mark_chain_recovered(const std::string& chain_name);
    void print_console_header();
    void print_console_row(const std::string& chain_name,
                           double balance_coin,
                           const std::string& address,
                           const std::string& mnemonic_words);
    void run_manual_wallet_checks();
    void run_balance_checker();
    std::optional<std::pair<std::string, std::string>> parse_manual_wallet_line(const std::string& line) const;
    void persist_recovered_wallet(const std::string& chain_name,
                                  const std::string& address,
                                  const core::Mnemonic& mnemonic,
                                  double balance_coin,
                                  const std::string& coin_ticker) const;
    void persist_recovered_wallet_from_phrase(const std::string& chain_name,
                                              const std::string& address,
                                              const std::string& mnemonic_phrase,
                                              double balance_coin,
                                              const std::string& coin_ticker) const;
    void enqueue_wallet_candidate(const std::string& chain_name,
                                  const std::string& address,
                                  const std::string& mnemonic_words);
    std::optional<std::tuple<std::string, std::string, std::string>> parse_wallet_queue_line(const std::string& line) const;
};

} // namespace engine
