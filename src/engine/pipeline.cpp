#include "engine/pipeline.hpp"

#include "core/secure_buffer.hpp"
#include "core/thread_pool.hpp"

#include <openssl/evp.h>

#include <iostream>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <vector>

namespace engine {

namespace {
core::SecureBuffer mnemonic_to_seed(const core::Mnemonic& mnemonic, const std::string& passphrase) {
    std::ostringstream joined;
    for (std::size_t i = 0; i < mnemonic.size(); ++i) {
        joined << mnemonic[i];
        if (i + 1 < mnemonic.size()) {
            joined << ' ';
        }
    }

    const std::string phrase = joined.str();
    const std::string salt = "mnemonic"; // empty passphrase for educational skeleton

    std::vector<std::uint8_t> seed(64, 0);
    const int ok = PKCS5_PBKDF2_HMAC(
        phrase.c_str(),
        static_cast<int>(phrase.size()),
        reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size()),
        2048,
        EVP_sha512(),
        static_cast<int>(seed.size()),
        seed.data());

    if (ok != 1) {
        throw std::runtime_error("Failed to derive BIP-39 seed");
    }

    return core::SecureBuffer(std::move(seed));
}

std::vector<std::string> paths_for_module(const core::AppConfig& cfg, const std::string& name) {
    if (name == "btc") return cfg.paths_btc;
    if (name == "eth") return cfg.paths_eth;
    if (name == "sol") return cfg.paths_sol;
    return {};
}

std::string mnemonic_to_string(const core::Mnemonic& mnemonic) {
    std::ostringstream joined;
    for (std::size_t i = 0; i < mnemonic.size(); ++i) {
        joined << mnemonic[i];
        if (i + 1 < mnemonic.size()) {
            joined << ' ';
        }
    }
    return joined.str();
}

std::string trim_copy(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string escape_sql_literal(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\'') {
            out += "''";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string wallet_record_key(const std::string& chain_name,
                              const std::string& address,
                              const std::string& mnemonic_words) {
    return chain_name + "|" + address + "|" + mnemonic_words;
}

std::string seed_record_key(const std::string& chain_name,
                            const std::string& mnemonic_words) {
    return "seed|" + chain_name + "|" + mnemonic_words;
}

const std::string& seed_table_for_chain(const core::AppConfig& cfg, const std::string& chain_name) {
    if (chain_name == "btc") return cfg.postgres_seed_table_btc;
    if (chain_name == "eth") return cfg.postgres_seed_table_evm;
    if (chain_name == "sol") return cfg.postgres_seed_table_sol;
    throw std::runtime_error("No PostgreSQL seed table configured for chain: " + chain_name);
}

const std::string& result_table_for_chain(const core::AppConfig& cfg, const std::string& chain_name) {
    if (chain_name == "btc") return cfg.postgres_result_table_btc;
    if (chain_name == "eth") return cfg.postgres_result_table_evm;
    if (chain_name == "sol") return cfg.postgres_result_table_sol;
    throw std::runtime_error("No PostgreSQL result table configured for chain: " + chain_name);
}

std::string shell_quote_arg(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
#ifdef _WIN32
    out.push_back('"');
    for (char c : value) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
#else
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
#endif
    return out;
}

FILE* popen_read(const std::string& command) {
#ifdef _WIN32
    return _popen(command.c_str(), "r");
#else
    return popen(command.c_str(), "r");
#endif
}

int pclose_read(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

bool is_valid_sql_identifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const auto is_ident_char = [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    };
    if (!std::isalpha(static_cast<unsigned char>(value.front())) && value.front() != '_') {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), is_ident_char);
}

void exec_psql_command(const std::string& conninfo, const std::string& sql) {
    const std::string cmd = "psql " + shell_quote_arg(conninfo) +
                            " -v ON_ERROR_STOP=1 -q -c " + shell_quote_arg(sql);
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        throw std::runtime_error("Failed to execute PostgreSQL command via psql. Exit code: " + std::to_string(rc));
    }
}

} // namespace

Pipeline::Pipeline(const core::AppConfig& config,
                   const bip39::MnemonicValidator& validator,
                   const bip39::MnemonicGenerator& generator)
    : config_(config), validator_(validator), generator_(generator) {}

void Pipeline::register_chain(std::unique_ptr<chains::IChainModule> module) {
    modules_.push_back(std::move(module));
}

bool Pipeline::is_chain_recovered(const std::string& chain_name) {
    std::lock_guard<std::mutex> lock(recovered_mutex_);
    return recovered_chains_.contains(chain_name);
}

void Pipeline::mark_chain_recovered(const std::string& chain_name) {
    std::lock_guard<std::mutex> lock(recovered_mutex_);
    recovered_chains_.insert(chain_name);
}

void Pipeline::print_console_header() {
    bool expected = false;
    if (!console_header_printed_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::lock_guard<std::mutex> lock(console_mutex_);
    std::cout << "# || WALLET || ADDRESS || SEED\n";
}

void Pipeline::print_console_row(const std::string& chain_name,
                                 const std::string& address,
                                 const std::string& mnemonic_words) {
    std::lock_guard<std::mutex> lock(console_mutex_);
    ++console_wallet_counter_;
    std::cout << console_wallet_counter_ << " || " << chain_name << " || " << address << " || " << mnemonic_words
              << '\n';
}

void Pipeline::persist_wallet_record(const std::string& chain_name,
                                     const std::string& address,
                                     const core::Mnemonic& mnemonic) const {
    const std::string mnemonic_words = mnemonic_to_string(mnemonic);
    const std::string key = wallet_record_key(chain_name, address, mnemonic_words);

    {
        std::lock_guard<std::mutex> lock(wallet_cache_mutex_);
        if (known_wallet_records_.contains(key)) {
            return;
        }
        if (config_.postgres_conninfo.empty()) {
            known_wallet_records_.insert(key);
            return;
        }
    }

    const std::string& result_table = result_table_for_chain(config_, chain_name);
    if (!is_valid_sql_identifier(result_table)) {
        throw std::runtime_error("Invalid PostgreSQL table name: " + result_table);
    }
    const std::string insert =
        "INSERT INTO " + result_table +
        " (blockchain, address, mnemonic) VALUES ('" +
        escape_sql_literal(chain_name) + "', '" +
        escape_sql_literal(address) + "', '" +
        escape_sql_literal(mnemonic_words) + "') ON CONFLICT DO NOTHING;";

    std::lock_guard<std::mutex> lock(postgres_mutex_);
    exec_psql_command(config_.postgres_conninfo, insert);
    std::lock_guard<std::mutex> cache_lock(wallet_cache_mutex_);
    known_wallet_records_.insert(key);
}

bool Pipeline::mark_seed_phrase_if_new(const std::string& chain_name,
                                       const std::string& mnemonic_words) const {
    const std::string key = seed_record_key(chain_name, mnemonic_words);
    {
        std::lock_guard<std::mutex> lock(wallet_cache_mutex_);
        if (known_wallet_records_.contains(key)) {
            return false;
        }
    }

    if (config_.postgres_conninfo.empty()) {
        std::lock_guard<std::mutex> lock(wallet_cache_mutex_);
        known_wallet_records_.insert(key);
        return true;
    }

    const std::string& table = seed_table_for_chain(config_, chain_name);
    if (!is_valid_sql_identifier(table)) {
        throw std::runtime_error("Invalid PostgreSQL seed table name: " + table);
    }

    const std::string insert =
        "INSERT INTO " + table + " (mnemonic) VALUES ('" +
        escape_sql_literal(mnemonic_words) + "') ON CONFLICT DO NOTHING RETURNING id;";

    std::lock_guard<std::mutex> lock(postgres_mutex_);
    const std::string cmd = "psql " + shell_quote_arg(config_.postgres_conninfo) +
                            " -v ON_ERROR_STOP=1 -At -q -c " + shell_quote_arg(insert);
    FILE* pipe = popen_read(cmd);
    if (pipe == nullptr) {
        throw std::runtime_error("Failed to execute PostgreSQL seed dedup command.");
    }

    std::string output;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int rc = pclose_read(pipe);
    if (rc != 0) {
        throw std::runtime_error("Failed to execute PostgreSQL seed dedup command. Exit code: " + std::to_string(rc));
    }

    const bool inserted = !trim_copy(output).empty();
    if (inserted) {
        std::lock_guard<std::mutex> cache_lock(wallet_cache_mutex_);
        known_wallet_records_.insert(key);
    }
    return inserted;
}

void Pipeline::run() {
    std::cout << std::unitbuf;

    core::ThreadPool pool(config_.threads);
    print_console_header();
    std::atomic<std::uint64_t> processed_candidates{0};
    std::atomic<std::uint64_t> stored_wallets{0};
    const std::size_t valid_candidates = generator_.generate(
        config_.template_words,
        validator_,
        config_.max_candidates,
        [&](const core::Mnemonic& mnemonic) {
            ++processed_candidates;

            const std::string mnemonic_words = mnemonic_to_string(mnemonic);
            auto seed = mnemonic_to_seed(mnemonic, config_.bip39_passphrase);
            struct ChainMatchResult {
                std::string chain_name;
                std::optional<std::string> matched_address;
            };

            std::vector<std::future<ChainMatchResult>> futures;
            futures.reserve(modules_.size());

            for (const auto& module : modules_) {
                if (is_chain_recovered(module->name())) {
                    continue;
                }
                auto paths = paths_for_module(config_, module->name());
                futures.push_back(pool.enqueue([&, paths, module_ptr = module.get(), seed_copy = seed, mnemonic_words]() mutable {
                    if (!mark_seed_phrase_if_new(module_ptr->name(), mnemonic_words)) {
                        return ChainMatchResult{module_ptr->name(), std::nullopt};
                    }
                    auto derived = module_ptr->derive_addresses(seed_copy, paths, config_.scan_limit);

                    for (const auto& address : derived) {
                        persist_wallet_record(module_ptr->name(), address, mnemonic);
                        ++stored_wallets;
                        print_console_row(module_ptr->name(), address, mnemonic_words);
                    }
                    return ChainMatchResult{module_ptr->name(), std::nullopt};
                }));
            }

            if (futures.empty()) return true;

            for (auto& future : futures) {
                auto result = future.get();
                if (!result.matched_address.has_value()) {
                    continue;
                }

                if (is_chain_recovered(result.chain_name)) {
                    continue;
                }

                mark_chain_recovered(result.chain_name);
                const auto module_it = std::find_if(modules_.begin(), modules_.end(), [&](const auto& module) {
                    return module->name() == result.chain_name;
                });
                (void)module_it;
                std::cout << "Matched " << result.chain_name << " wallet: " << *result.matched_address << '\n';
            }

            return false;
        });

    {
        std::lock_guard<std::mutex> lock(console_mutex_);
        std::cout << "Done. Valid candidates: " << valid_candidates
                  << ", processed: " << processed_candidates.load()
                  << ", stored wallets: " << stored_wallets.load() << ".\n";
        if (valid_candidates == 0) {
            std::cout << "Hint: search finished immediately because there were no valid candidates for the current "
                         "template/wordlist settings.\n";
        }
    }
}

} // namespace engine
