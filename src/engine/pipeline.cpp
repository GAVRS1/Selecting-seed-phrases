#include "engine/pipeline.hpp"

#include "core/secure_buffer.hpp"
#include "core/thread_pool.hpp"

#include <openssl/evp.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
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
    const std::string salt = "mnemonic" + passphrase;

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
} // namespace

Pipeline::Pipeline(const core::AppConfig& config,
                   const bip39::MnemonicValidator& validator,
                   const bip39::MnemonicGenerator& generator,
                   Matcher& matcher)
    : config_(config), validator_(validator), generator_(generator), matcher_(matcher) {}

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

void Pipeline::persist_recovered_wallet(const std::string& chain_name,
                                        const std::string& address,
                                        const core::Mnemonic& mnemonic) const {
    std::ofstream out(config_.recovered_wallets_path, std::ios::app);
    if (!out) {
        throw std::runtime_error("Failed to open recovered wallets file: " + config_.recovered_wallets_path);
    }

    out << "chain: " << chain_name << '\n';
    out << "address: " << address << '\n';
    out << "mnemonic: " << mnemonic_to_string(mnemonic) << "\n\n";
}

void Pipeline::run() {
    core::ThreadPool pool(config_.threads);

    generator_.generate(
        config_.template_words,
        validator_,
        config_.max_candidates,
        [&](const core::Mnemonic& mnemonic) {
            if (matcher_.should_stop()) {
                return true;
            }

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
                futures.push_back(pool.enqueue([&, paths, module_ptr = module.get(), seed_copy = seed]() mutable {
                    auto derived = module_ptr->derive_addresses(seed_copy, paths, config_.scan_limit);
                    return ChainMatchResult{
                        module_ptr->name(),
                        matcher_.find_match(derived),
                    };
                }));
            }

            if (futures.empty()) {
                matcher_.stop();
                return true;
            }

            for (auto& future : futures) {
                auto result = future.get();
                if (!result.matched_address.has_value()) {
                    continue;
                }

                if (is_chain_recovered(result.chain_name)) {
                    continue;
                }

                mark_chain_recovered(result.chain_name);
                persist_recovered_wallet(result.chain_name, *result.matched_address, mnemonic);
                std::cout << "Recovered " << result.chain_name << " wallet: " << *result.matched_address << '\n';
            }

            return false;
        });
}

} // namespace engine
