#pragma once

#include "bip39/mnemonic_generator.hpp"
#include "bip39/mnemonic_validator.hpp"
#include "chains/i_chain_module.hpp"
#include "core/types.hpp"
#include "engine/matcher.hpp"

#include <memory>
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
};

} // namespace engine
