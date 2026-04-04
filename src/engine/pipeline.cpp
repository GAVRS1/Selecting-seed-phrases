#include "engine/pipeline.hpp"

#include "core/secure_buffer.hpp"
#include "core/thread_pool.hpp"

#include <iostream>
#include <sstream>

namespace engine {

namespace {
core::SecureBuffer mnemonic_to_seed(const core::Mnemonic& mnemonic) {
    std::ostringstream joined;
    for (const auto& word : mnemonic) {
        joined << word << ' ';
    }
    const auto str = joined.str();
    std::vector<std::uint8_t> bytes(str.begin(), str.end());
    return core::SecureBuffer(std::move(bytes));
}

std::vector<std::string> paths_for_module(const core::AppConfig& cfg, const std::string& name) {
    if (name == "btc") return cfg.paths_btc;
    if (name == "eth") return cfg.paths_eth;
    if (name == "sol") return cfg.paths_sol;
    return {};
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

            auto seed = mnemonic_to_seed(mnemonic);
            std::vector<std::future<bool>> futures;
            futures.reserve(modules_.size());

            for (const auto& module : modules_) {
                auto paths = paths_for_module(config_, module->name());
                futures.push_back(pool.enqueue([&, paths, module_ptr = module.get(), seed_copy = seed]() mutable {
                    auto derived = module_ptr->derive_addresses(seed_copy, paths, config_.scan_limit);
                    return matcher_.contains(derived);
                }));
            }

            for (auto& future : futures) {
                if (future.get()) {
                    matcher_.stop();
                    std::cout << "Match found. Stop requested.\n";
                    return true;
                }
            }
            return false;
        });
}

} // namespace engine
