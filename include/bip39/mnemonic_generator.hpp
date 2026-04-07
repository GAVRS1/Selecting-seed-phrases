#pragma once

#include "bip39/mnemonic_validator.hpp"
#include "bip39/wordlist.hpp"
#include "core/types.hpp"

#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bip39 {

class MnemonicGenerator {
public:
    MnemonicGenerator(const Wordlist& wordlist,
                      std::vector<std::string> allow_words,
                      std::optional<std::uint64_t> shuffle_seed = std::nullopt);

    std::size_t generate(
        const core::Mnemonic& pattern,
        const MnemonicValidator& validator,
        std::uint64_t max_candidates,
        const std::function<bool(const core::Mnemonic&)>& on_valid_candidate) const;

private:
    const Wordlist& wordlist_;
    std::vector<std::string> allow_words_;
    std::optional<std::uint64_t> shuffle_seed_;
};

} // namespace bip39
